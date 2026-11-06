#include "SourceCatalog.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace jade::native;
namespace fs=std::filesystem;
int main(){try{
 const auto root=fs::temp_directory_path()/"jade-native-empty-schema-tests";
 fs::remove_all(root);fs::create_directories(root/"schemas");
 const auto schema=root/"schemas"/"Pets.scm";
 std::ofstream(schema)<<"jadeVersionNumber \"25.0.02\";\nschemaDefinition\nPets subschemaOf RootSchema completeDefinition;\n";
 SourceCatalog catalog;
 auto inputs=catalog.buildInputs({root});
 if(inputs.size()!=1||inputs[0]!=schema)throw std::runtime_error("Bare build must select schema headers when no class files exist");
 const auto cls=root/"schemas"/"Pet.cls";std::ofstream(cls)<<"fixture";
 inputs=catalog.buildInputs({root});if(inputs.size()!=1||inputs[0]!=cls)throw std::runtime_error("Class builds must preserve dependency selection instead of treating headers as test targets");
 fs::remove_all(root);std::cout<<"SourceCatalog build inputs: PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
