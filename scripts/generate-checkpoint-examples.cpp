#include "checkpoint-examples.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv){try{if(argc!=2)throw std::invalid_argument("Provide an existing empty output directory");const std::filesystem::path dir=argv[1];if(!std::filesystem::is_directory(dir))throw std::invalid_argument("Output directory must exist");
    const std::array names{"forklift","workers","loading-bay","no-benefit","surge"};const auto examples=checkpoint_examples();
    for(std::size_t i=0;i<examples.size();++i){const auto path=dir/(std::string(names[i])+".json");std::ofstream file(path,std::ios::binary|std::ios::noreplace);if(!file)throw std::runtime_error("Cannot exclusively create request");file<<simforge::analysis::checkpoint::request_json(examples[i]);file.close();const auto report=simforge::analysis::checkpoint::run(examples[i]);simforge::analysis::checkpoint::save_report(dir/(std::string(names[i])+"-report.json"),report);std::cout<<names[i]<<" mean paired completed "<<*report.end_metrics[0].mean_difference<<'\n';}return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
