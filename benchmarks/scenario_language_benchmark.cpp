#include <simforge/scenario/scenario_language.hpp>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#endif
namespace
{
thread_local bool measuring=false;
std::uint64_t allocations=0,allocated_bytes=0;
}
void* operator new(std::size_t size)
{
    if(auto* p=std::malloc(size?size:1)) {if(measuring){++allocations;allocated_bytes+=size;}return p;}throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
std::size_t peak()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters));return counters.PeakWorkingSetSize;
#else
    return 0;
#endif
}
int main()
{
    namespace nl=simforge::scenario::language;
    std::string maximum;while(maximum.size()+22<=nl::max_input_bytes) maximum+="add one forklift and ";maximum.resize(nl::max_input_bytes,' ');
    const std::array inputs{
        std::pair{"create",std::string{"Create a warehouse with 4 loading bays, 12 workers and 3 forklifts. Shipments arrive every 5 minutes and take 15 minutes to process. Run the simulation for 8 hours."}},
        std::pair{"modify",std::string{"Add two forklifts and reduce processing duration by 20%."}},
        std::pair{"unsupported",std::string{"Use four forklifts and add autonomous robot navigation."}},
        std::pair{"maximum_input",maximum}};
    const auto source=nl::default_source();
    std::cout<<"case,bytes,iterations,tokenize_us,recognize_us,apply_us,validate_us,total_mean_us,total_worst_us,allocations_mean,allocated_bytes_mean,process_peak_working_set_bytes\n";
    for(const auto& [name,text]:inputs) {
        nl::Timings sums;double worst=0;std::uint64_t allocation_sum=0,bytes_sum=0;
        constexpr int iterations=1000;
        for(int i=0;i<iterations;++i) {
            allocations=allocated_bytes=0;measuring=true;
            const auto p=nl::interpret(text,std::string_view(name)=="create"?nl::Mode::Create:nl::Mode::Modify,source,{1,1});
            measuring=false;allocation_sum+=allocations;bytes_sum+=allocated_bytes;
            sums.tokenize_us+=p.timings().tokenize_us;sums.recognize_us+=p.timings().recognize_us;sums.apply_us+=p.timings().apply_us;
            sums.validate_us+=p.timings().validate_us;sums.total_us+=p.timings().total_us;worst=std::max(worst,p.timings().total_us);
        }
        std::cout<<std::fixed<<std::setprecision(3)<<name<<','<<text.size()<<','<<iterations<<','<<sums.tokenize_us/iterations<<','<<sums.recognize_us/iterations<<','<<sums.apply_us/iterations
            <<','<<sums.validate_us/iterations<<','<<sums.total_us/iterations<<','<<worst<<','<<allocation_sum/iterations<<','<<bytes_sum/iterations<<','<<peak()<<'\n';
    }
}
