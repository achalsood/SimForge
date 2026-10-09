#include <simforge/workspace/scenario_workspace.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace simforge;
using namespace simforge::workspace;
using Timer=std::chrono::steady_clock;
double ms(Timer::time_point start) { return std::chrono::duration<double,std::milli>{Timer::now()-start}.count(); }
int main()
{
#ifndef NDEBUG
    std::cerr<<"Workspace benchmark requires Release\n"; return 1;
#else
    try
    {
        std::cout<<"10K arrivals; three trials, median; capacities 4/8/2; current checkpoint 5000 s. No file I/O or GPU.\n"
            <<"candidates,validation_ms,creation_ms,fresh_comparison_ms,fresh_rank_ms,checkpoint_ms,json_ms,csv_ms,view_switch_ms,checksum\n";
        for(int count:{3,10,50})
        {
            std::array<std::array<double,8>,3> samples{}; std::size_t checksum=0;
            for(auto& values:samples)
            {
                scenario::ScenarioDefinition d; d.name="Workspace benchmark"; d.simulation={60,10000,42}; d.domain=scenario::WarehouseConfig{4,8,2,1,4};
                ScenarioWorkspace w{d}; auto start=Timer::now();
                for(int i=0;i<100;++i) if(!w.validate_draft()) throw std::runtime_error("Invalid benchmark draft"); values[0]=ms(start)/100;
                start=Timer::now();
                for(int i=0;i<count;++i) { w.edit(Field::Name,"Candidate "+std::to_string(i)); w.edit(Field::Bays,std::to_string(4+i%4)); w.edit(Field::Workers,std::to_string(8+i%5)); w.edit(Field::Forklifts,std::to_string(2+i%4)); w.add_candidate(); }
                values[1]=ms(start)/count;
                start=Timer::now(); w.compare(ComparisonMode::Initial,analysis::Objective::MaximizeCompleted); values[2]=ms(start); values[3]=w.report()->report_ms;
                start=Timer::now(); w.view_result(w.report()->candidates.front().candidate.name); values[7]=ms(start); w.return_to_baseline();
                start=Timer::now(); const auto json=w.json(); values[5]=ms(start); start=Timer::now(); const auto csv=w.csv(); values[6]=ms(start);
                checksum+=json.size()+csv.size()+w.report()->candidates.front().metrics.completed;
                w.view().seek(5000*60); start=Timer::now(); w.compare(ComparisonMode::Checkpoint,analysis::Objective::MaximizeCompleted); values[4]=ms(start);
                if(w.report()->baseline.metrics.arrived!=10000) throw std::runtime_error("Benchmark conservation mismatch");
            }
            std::cout<<count<<std::fixed<<std::setprecision(6);
            for(std::size_t col=0;col<8;++col) { std::array<double,3> sorted{samples[0][col],samples[1][col],samples[2][col]}; std::sort(sorted.begin(),sorted.end()); std::cout<<','<<sorted[1]; }
            std::cout<<','<<checksum<<'\n';
        }
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
#endif
}
