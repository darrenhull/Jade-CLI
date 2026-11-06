// Native test-session host. Native Jade owns test discovery, lifecycle and
// reporting.
#include "../inspector/runtime.hpp"
#include "../inspector/protocol.hpp"
#include <chrono>
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
bool inside(const std::filesystem::path &path,
            const std::filesystem::path &root) {
  auto relative = path.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != L"..";
}
std::filesystem::path canonical(const std::string &path) {
  return std::filesystem::weakly_canonical(
      std::filesystem::absolute(std::filesystem::path(wide(path))));
}
void validate(const json &config) {
  auto root = canonical(config.at("root")),
       database = canonical(config.at("databasePath"));
  if (!inside(database, root))
    throw std::runtime_error(
        "Test database must be inside its private working root.");
  auto source = canonical(config.at("sourceDatabasePath"));
  if (database == source ||
      (std::filesystem::exists(source) && std::filesystem::exists(database) &&
       std::filesystem::equivalent(database, source)))
    throw std::runtime_error(
        "Native test sessions cannot open the source database.");
}
std::string privateFile(const json &config, const json &request,
                        const char *name, bool exists) {
  auto value = request.at(name).get<std::string>();
  auto path = canonical(value);
  if (!inside(path, canonical(config.at("root"))))
    throw std::runtime_error(std::string(name) +
                             " must be inside the private working root.");
  if (exists && !std::filesystem::is_regular_file(path))
    throw std::runtime_error("Native test control file does not exist.");
  return utf8(path.wstring());
}
Oid createRunner(Runtime &r) {
  Oid value{};
  value.classNo = DSKJADETESTRUNNER;
  r.check(proc<decltype(&jomCreateObject)>(r.jom, "jomCreateObject")(
              &r.process, &value, CLASS_TRANSIENT, nullptr, nullptr, 0),
          "Create native JadeTestRunner");
  return value;
}
bool readLine(std::string &line) {
  line.clear();
  char c;
  while (std::cin.get(c)) {
    if (c == '\n')
      return true;
    if (line.size() >= 1024 * 1024)
      throw std::runtime_error("Native test request exceeds its size limit.");
    line += c;
  }
  return !line.empty();
}

std::string nativeText(Param &value) {
  if (value.value.header.format == DSKCSTRING)
    return utf8(value.value.body.pText);
  Size length{};
  if (JADE_STRING_LENGTH(&value.value.body.dataHandler, length))
    throw std::runtime_error("Cannot read native String length.");
  std::wstring result(length + 1, 0);
  if (JADE_GET_STRING(&value.value.body.dataHandler, result.data(), length + 1))
    throw std::runtime_error("Cannot read native String.");
  result.resize(length);
  return utf8(result);
}
std::string loaderLog(const std::filesystem::path &file, uintmax_t offset) {
  if (!std::filesystem::exists(file))
    return {};
  auto size = std::filesystem::file_size(file);
  if (offset > size)
    offset = 0;
  std::ifstream stream(file, std::ios::binary);
  unsigned char bom[2]{};
  stream.read(reinterpret_cast<char *>(bom), 2);
  bool unicode = bom[0] == 255 && bom[1] == 254;
  bool truncated = size - offset > 2 * 1024 * 1024;
  if (truncated)
    offset = size - 2 * 1024 * 1024;
  if (unicode && offset % 2)
    ++offset;
  if (unicode && offset == 0)
    offset = 2;
  stream.clear();
  stream.seekg(offset);
  std::string bytes((std::istreambuf_iterator<char>(stream)), {}), text;
  if (unicode) {
    std::wstring wideText;
    wideText.reserve(bytes.size() / 2);
    for (size_t i = 0; i + 1 < bytes.size(); i += 2)
      wideText +=
          wchar_t(static_cast<unsigned char>(bytes[i]) |
                  (unsigned(static_cast<unsigned char>(bytes[i + 1])) << 8));
    text = utf8(wideText);
  } else {
    text = std::move(bytes);
    if (text.starts_with("\xEF\xBB\xBF"))
      text.erase(0, 3);
  }
  return (truncated ? "Native loader log exceeds the response limit; earlier "
                      "output omitted.\n"
                    : "") +
         text;
}
json load(Runtime &r, const json &config, const json &request) {
  const auto &arguments = request.at("args");
  if (!arguments.is_array() || arguments.empty() || arguments.size() > 512)
    throw std::runtime_error("Native load requires1to512 arguments.");
  for (const auto &item : arguments) {
    auto argument = item.get<std::string>();
    if (argument.empty() || argument.size() > 65536 ||
        argument.find('\0') != std::string::npos)
      throw std::runtime_error("Invalid native loader argument.");
    auto equal = argument.find('=');
    if (equal == std::string::npos)
      throw std::runtime_error("Native loader arguments require name=value.");
    auto name = argument.substr(0, equal), value = argument.substr(equal + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (name == "path" || name == "ini" || name == "server" ||
        name == "schema" || name == "app")
      throw std::runtime_error(
          "Runtime connection arguments cannot be changed by native loads.");
    if (name.ends_with("file") || name.ends_with("filename") ||
        name.ends_with("directory") || name.ends_with("folder")) {
      if (!inside(canonical(value), canonical(config.at("root"))) &&
          !(config.contains("assemblyRoot") &&
            inside(canonical(value), canonical(config.at("assemblyRoot")))))
        throw std::runtime_error(
            "Loader files must remain inside the private working root.");
    }
  }
  auto log = canonical(request.value(
      "logFile",
      utf8((canonical(config.at("root")) / L"logs" / L"jadeschemaloader.log")
               .wstring())));
  if (!inside(log, canonical(config.at("root"))))
    throw std::runtime_error(
        "Loader log must remain inside private working root.");
  auto offset =
      std::filesystem::exists(log) ? std::filesystem::file_size(log) : 0;
  // The loader summary omits structural source positions. Jade records those
  // in compiler.log; capture only this load's bounded delta, not prior errors.
  auto compilerLog = canonical(config.at("root")) / L"logs" / L"compiler.log";
  auto compilerOffset = std::filesystem::exists(compilerLog)
                            ? std::filesystem::file_size(compilerLog)
                            : 0;
  auto start = Clock::now();
  Oid parameters{};
  parameters.classNo = DSKHUGESTRINGARRAY;
  r.check(proc<decltype(&jomBeginTransaction)>(r.jom, "jomBeginTransaction")(
              &r.process, TRANSIENT_TRANSACTION, 0),
          "Begin native loader parameters");
  try {
    r.check(proc<decltype(&jomCreateObject)>(r.jom, "jomCreateObject")(
                &r.process, &parameters, CLASS_SHARED_TRANSIENT, nullptr,
                nullptr, 0),
            "Create native loader parameter array");
    for (auto &argument : arguments) {
      auto value = Param::string(argument);
      Param ignored;
      r.send(parameters, "add", {&value}, ignored);
    }
    r.check(proc<decltype(&jomEndTransaction)>(r.jom, "jomEndTransaction")(
                &r.process, TRANSIENT_TRANSACTION, 0),
            "Commit native loader parameters");
  } catch (...) {
    proc<decltype(&jomAbortTransaction)>(r.jom, "jomAbortTransaction")(
        &r.process, TRANSIENT_TRANSACTION, 0);
    throw;
  }
  auto cleanup = [&]() {
    r.check(proc<decltype(&jomBeginTransaction)>(r.jom, "jomBeginTransaction")(
                &r.process, TRANSIENT_TRANSACTION, 0),
            "Begin loader parameter release");
    r.check(proc<decltype(&jomDeleteObject)>(r.jom, "jomDeleteObject")(
                &r.process, &parameters, nullptr, nullptr, 0),
            "Release native loader parameters");
    r.check(proc<decltype(&jomEndTransaction)>(r.jom, "jomEndTransaction")(
                &r.process, TRANSIENT_TRANSACTION, 0),
            "Commit loader parameter release");
  };
  int code{};
  try {
    auto schema = Param::string("RootSchema"),
         application = Param::string("JadeSchemaLoader"),
         args = Param::oid(parameters);
    Param process(DSKOBJECTID);
    r.send(r.instances[1], "startApplicationWithParameter",
           {&schema, &application, &args}, process);
    auto nativeProcess = process.object();
    if (nullOid(nativeProcess))
      throw std::runtime_error("Native schema loader did not start.");
    bool valid = true;
    while (valid) {
      auto processValue = Param::oid(nativeProcess);
      Param isValid(DSKBOOLEAN);
      r.send(r.instances[1], "isValidObject", {&processValue}, isValid);
      valid = isValid.value.body.boolean;
      if (valid)
        Sleep(10);
    }
    Any result(r);
    r.send(parameters, "last", {}, result);
    auto status = nativeText(result);
    if (!status.starts_with("result="))
      throw std::runtime_error("Native loader did not return a result code.");
    size_t length = 0;
    code = std::stoi(status.substr(7), &length);
    if (length != status.size() - 7)
      throw std::runtime_error("Invalid native loader result code.");
    cleanup();
  } catch (...) {
    try {
      cleanup();
    } catch (...) {
    }
    throw;
  }
  return {{"code", code},
          {"success", code == 0},
          {"loadMilliseconds", milliseconds(start)},
          {"compilerOutput", loaderLog(compilerLog, compilerOffset)},
          {"output", loaderLog(log, offset)}};
}
int wmain(int argc, wchar_t **argv) {
  ProtocolOutput output;
  auto emit = [&](json value) {
    value["protocol"] = "jade-test/1";
    output.write(value);
  };
  try {
    output.initialize();
    if (argc != 2)
      throw std::runtime_error("Expected native test session JSON file.");
    json config;
    {
      std::ifstream file{std::filesystem::path(argv[1])};
      file >> config;
    }
    validate(config);
    config["schema"] = "RootSchema";
    std::unique_ptr<Runtime> runtime;
    unsigned connection = 0;
    emit({{"event", "ready"}, {"workingDatabase", true}});
    std::string line;
    while (readLine(line)) {
      json request;
      try {
        request = json::parse(line);
        auto id = request.at("id");
        if (!id.is_number_integer() || id.get<int>() <= 0)
          throw std::runtime_error(
              "Native test request requires a positive integer id.");
        auto command = request.at("command").get<std::string>();
        if (command == "stop" || command == "close") {
          runtime.reset();
          emit({{"id", id}, {"success", true}, {"body", {{"closed", true}}}});
          return 0;
        }
        if (command != "run" && command != "load")
          throw std::runtime_error("Unknown native test command.");
        double openMs = 0;
        if (!runtime) {
          auto start = Clock::now();
          runtime = std::make_unique<Runtime>(config, true);
          openMs = milliseconds(start);
          ++connection;
        }
        auto &r = *runtime;
        if (command == "load") {
          auto body = load(r, config, request);
          body["connection"] = connection;
          body["openMilliseconds"] = openMs;
          emit({{"id", id}, {"success", true}, {"body", body}});
          continue;
        }
        auto control = Param::string(
                 privateFile(config, request, "controlFile", true)),
             log =
                 Param::string(privateFile(config, request, "logFile", false));
        auto testName =
            request.value("testName", std::string("WorkspaceTests"));
        if (testName.empty() || testName.size() > 4096 ||
            testName.find('\0') != std::string::npos)
          throw std::runtime_error("Invalid native test selection name.");
        auto name = Param::string(testName);
        auto runner = createRunner(r);
        auto start = Clock::now();
        try {
          Param ignored;
          r.send(runner, "run", {&control, &name, &log}, ignored);
        } catch (...) {
          proc<decltype(&jomDeleteObject)>(r.jom, "jomDeleteObject")(
              &r.process, &runner, nullptr, nullptr, 0);
          throw;
        }
        double runMs = milliseconds(start);
        r.check(proc<decltype(&jomDeleteObject)>(r.jom, "jomDeleteObject")(
                    &r.process, &runner, nullptr, nullptr, 0),
                "Release native JadeTestRunner");
        emit({{"id", id},
              {"success", true},
              {"body",
               {{"code", 0},
                {"success", true},
                {"connection", connection},
                {"openMilliseconds", openMs},
                {"runMilliseconds", runMs}}}});
      } catch (const std::exception &e) {
        runtime.reset();
        emit({{"id", request.is_object() ? request.value("id", 0) : 0},
              {"success", false},
              {"message", e.what()},
              {"code", 2}});
      }
    }
    return 0;
  } catch (const std::exception &e) {
    try {
      emit({{"event", "error"}, {"message", e.what()}});
    } catch (...) {
    }
    return 2;
  }
}
