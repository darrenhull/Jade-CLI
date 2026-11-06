#pragma once
#include <io.h>
#include <cstdio>

// Native application code and security plugins may write to either the CRT stdout
// descriptor or the Win32 standard output handle. Keep protocol output on its own
// non-inherited duplicate before loading any native application/plugin code.
struct ProtocolOutput
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    bool owned = false, broken = false;

    void initialize()
    {
        pipe = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE duplicate = INVALID_HANDLE_VALUE;
        if (!pipe || pipe == INVALID_HANDLE_VALUE ||
            !DuplicateHandle(GetCurrentProcess(), pipe, GetCurrentProcess(), &duplicate,
                             0, FALSE, DUPLICATE_SAME_ACCESS))
            throw std::runtime_error("Cannot reserve the inspection protocol output pipe.");
        pipe = duplicate;
        owned = true;
        std::fflush(stdout);
        if (_dup2(_fileno(stderr), _fileno(stdout)) != 0 ||
            !SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE)))
            throw std::runtime_error("Cannot separate native application output from the inspection protocol.");
    }
    void write(const json &value)
    {
        if (broken)
            return;
        auto line = value.dump() + "\n";
        size_t offset = 0;
        while (offset < line.size())
        {
            DWORD written = 0;
            auto count = static_cast<DWORD>(std::min<size_t>(line.size() - offset, MAXDWORD));
            if (!WriteFile(pipe, line.data() + offset, count, &written, nullptr) || !written)
            {
                broken = true;
                throw std::runtime_error("Inspection protocol output pipe closed.");
            }
            offset += written;
        }
    }
    ~ProtocolOutput()
    {
        if (owned)
            CloseHandle(pipe);
    }
};
