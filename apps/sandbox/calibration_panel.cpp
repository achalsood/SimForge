#include "calibration_panel.hpp"
#include "save_dialog.hpp"
#include <simforge/workspace/experiment_execution.hpp>
#include <imgui.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {
using namespace simforge;
namespace demand=analysis::demand;
glm::vec2 center(){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
bool input(const char* label,std::string& text) {
    std::array<char,4097> value{};std::memcpy(value.data(),text.data(),std::min(text.size(),value.size()-1));
    ImGui::TextUnformatted(label);ImGui::PushID(label);ImGui::SetNextItemWidth(-1);const bool changed=ImGui::InputText("##value",value.data(),value.size());if(changed)text=value.data();ImGui::PopID();return changed;
}
std::uint64_t number(const std::string& s,std::uint64_t max=UINT64_MAX) {
    std::uint64_t value{};const auto p=std::from_chars(s.data(),s.data()+s.size(),value);
    if(p.ec!=std::errc{} || p.ptr!=s.data()+s.size() || value>max)throw std::invalid_argument("Enter an unsigned decimal integer in the supported range");return value;
}
template<class F> void action(CalibrationUI& ui,F f){try{f();ui.error.clear();}catch(const std::exception& e){ui.error=e.what();}}
void chart(const char* title,const char* units,const std::vector<double>& values,const std::vector<double>& other,const std::vector<std::string>& tips,
    const std::string& start,const std::string& end,float split=-1,bool signed_axis=false,const std::vector<double>& edges={},const std::vector<demand::CoverageStatus>& coverage={}) {
    ImGui::TextUnformatted(title);ImGui::TextDisabled("%s",units);
    const float width=std::max(100.0f,ImGui::GetContentRegionAvail().x),height=128;
    ImGui::InvisibleButton(title,{width,height+36});auto origin=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 a{origin.x+56,origin.y+5},b{origin.x+width-12,origin.y+height};
    double maximum=1;for(auto x:values)maximum=std::max(maximum,std::abs(x));for(auto x:other)maximum=std::max(maximum,std::abs(x));
    const auto y=[&](double value){return signed_axis?static_cast<float>((a.y+b.y)/2-value/maximum*(b.y-a.y)/2):static_cast<float>(b.y-value/maximum*(b.y-a.y));};
    draw->AddLine({a.x,a.y},{a.x,b.y},IM_COL32(140,150,160,255));draw->AddLine({a.x,y(0)},{b.x,y(0)},IM_COL32(140,150,160,255));
    char label[32]{};snprintf(label,sizeof(label),"%.2g",maximum);draw->AddText({origin.x,a.y},IM_COL32(180,190,200,255),label);draw->AddText({origin.x,y(0)},IM_COL32(180,190,200,255),"0");
    if(signed_axis){snprintf(label,sizeof(label),"-%.2g",maximum);draw->AddText({origin.x,b.y-12},IM_COL32(180,190,200,255),label);}
    if(!values.empty()) {
        const auto cell=(b.x-a.x)/static_cast<float>(values.size());
        const bool timed=edges.size()==values.size()+1;
        const auto x_edge=[&](std::size_t i){return timed?a.x+static_cast<float>(edges[i])*(b.x-a.x):a.x+static_cast<float>(i)*cell;};
        for(std::size_t i=0;i<values.size();++i) {
            const auto x=x_edge(i),end_x=x_edge(i+1);
            if(!std::isfinite(values[i])){const auto status=i<coverage.size()?coverage[i]:demand::CoverageStatus::Unobserved;const auto color=status==demand::CoverageStatus::Closed?IM_COL32(130,140,150,105):status==demand::CoverageStatus::Interrupted?IM_COL32(210,155,75,105):IM_COL32(145,100,175,105);draw->AddRectFilled({x,a.y},{end_x,b.y},color);}
            else {const auto zero=y(0),v=y(values[i]);draw->AddRectFilled({x+0.5f,std::min(zero,v)},{std::max(x+1,end_x-1),std::max(zero,v)+0.5f},IM_COL32(62,166,188,220));}
            if(i<other.size() && std::isfinite(other[i]))draw->AddLine({x,y(other[i])},{end_x,y(other[i])},IM_COL32(247,179,77,255),2);
        }
        if(ImGui::IsItemHovered()) {
            const auto mouse=ImGui::GetIO().MousePos;
            const auto raw=timed?static_cast<int>(std::upper_bound(edges.begin(),edges.end(),static_cast<double>((mouse.x-a.x)/(b.x-a.x)))-edges.begin())-1:static_cast<int>((mouse.x-a.x)/cell);
            const auto index=std::clamp(raw,0,static_cast<int>(values.size()-1));
            if(static_cast<std::size_t>(index)<tips.size())ImGui::SetTooltip("%s",tips[index].c_str());
        }
    }
    if(split>=0){auto x=a.x+split*(b.x-a.x);draw->AddLine({x,a.y},{x,b.y},IM_COL32(240,110,95,255),2);draw->AddText({x+3,a.y},IM_COL32(240,150,120,255),"Holdout");}
    draw->AddText({a.x,b.y+5},IM_COL32(180,190,200,255),start.c_str());const auto size=ImGui::CalcTextSize(end.c_str());draw->AddText({b.x-size.x,b.y+21},IM_COL32(180,190,200,255),end.c_str());
}
void metrics(const demand::Report& r) {
    if(ImGui::BeginTable("Calibration counts",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollX,{0,ImGui::GetTextLineHeightWithSpacing()*11+10},580)) {
        for(auto label:{"Quantity","Training fitted","Holdout fitted","Holdout constant baseline"}){ImGui::TableSetupColumn(label);}ImGui::TableHeadersRow();
        const auto row=[&](const char* title,double a,double b,double c){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(title);const bool prediction=std::string_view(title)!="Exposure (seconds)" && std::string_view(title)!="Observed arrivals";std::size_t i=0;const demand::Metrics* m[]{&r.training,&r.holdout,&r.baseline_holdout};for(auto v:{a,b,c}){ImGui::TableNextColumn();if(prediction && m[i]->unavailable_exposure_seconds)ImGui::TextUnformatted("Unavailable rate");else ImGui::Text("%.6g",v);++i;}};
        row("Exposure (seconds)",static_cast<double>(r.training.exposure_seconds),static_cast<double>(r.holdout.exposure_seconds),static_cast<double>(r.baseline_holdout.exposure_seconds));
        row("Observed arrivals",static_cast<double>(r.training.observed),static_cast<double>(r.holdout.observed),static_cast<double>(r.baseline_holdout.observed));
        row("Expected arrivals",r.training.expected,r.holdout.expected,r.baseline_holdout.expected);
        row("Observed - expected",r.training.signed_error,r.holdout.signed_error,r.baseline_holdout.signed_error);
        row("Absolute count error",r.training.absolute_error,r.holdout.absolute_error,r.baseline_holdout.absolute_error);
        row("Signed error / hour",r.training.error_per_hour,r.holdout.error_per_hour,r.baseline_holdout.error_per_hour);
        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("Poisson deviance");for(const auto* m:{&r.training,&r.holdout,&r.baseline_holdout}){ImGui::TableNextColumn();if(m->unavailable_exposure_seconds)ImGui::TextUnformatted("Unavailable rate");else if(m->deviance)ImGui::Text("%.6g",*m->deviance);else ImGui::TextUnformatted("Infinite (zero prediction)");}
        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted("Poisson log likelihood");for(const auto* m:{&r.training,&r.holdout,&r.baseline_holdout}){ImGui::TableNextColumn();if(m->unavailable_exposure_seconds)ImGui::TextUnformatted("Unavailable rate");else if(m->log_likelihood)ImGui::Text("%.6g",*m->log_likelihood);else ImGui::TextUnformatted("Negative infinite");}ImGui::EndTable();
    }
}
}
void draw_calibration(simforge::workspace::ScenarioWorkspace& w,CalibrationUI& ui,float) {
    const auto began=std::chrono::steady_clock::now();using namespace simforge;namespace demand=analysis::demand;
    const auto progress=w.calibration_execution().progress();
    if(ui.preview_job.valid() && ui.preview_job.wait_for(std::chrono::seconds{0})==std::future_status::ready)action(ui,[&]{auto job=ui.preview_job;ui.preview_job={};auto result=job.get();if(w.calibration_report() && result.calibration_id==w.calibration_report()->id)ui.preview=std::move(result);});
    ImGui::TextWrapped("Demand Calibration | Local CSV | UTC seconds | Explicit [start,end) coverage");
    if(ui.preview_job.valid()){const char* phases[]{"Validating fitted evidence","Generating Poisson arrivals","Building realization diagnostics"};const auto phase=ui.preview_progress?std::clamp(ui.preview_progress->phase.load(),0,2):0;ImGui::Text("Phase: %s | Generated arrivals: %llu (safety limit 100000)",phases[phase],ui.preview_progress?ui.preview_progress->generated.load():0);if(ImGui::Button("Cancel realization preview"))ui.preview_stop.request_stop();ui.preview_cancel=center();}
    if(progress.active){ImGui::Text("Phase: %s",progress.phase.c_str());if(ImGui::Button("Cancel calibration"))w.calibration_execution().cancel();ui.cancel=center();}
    if(!progress.error.empty())ImGui::TextWrapped("Last attempt: %s",progress.error.c_str());
    if(progress.cancelled)ImGui::TextUnformatted("Cancelled; previous completed report preserved.");
    for(int i=0;i<4;++i){if(i)ImGui::SameLine();const char* labels[]{"Import","Model & split","Validation","Scenario & exports"};if(ImGui::Button(labels[i])){ui.tab=i;ui.scroll_target=0;}ui.tabs[i]=center();}
    ImGui::Separator();
    ImGui::BeginChild("Demand calibration content",{0,0});
    if(ui.scroll_target==0){ImGui::SetScrollY(0);ui.scroll_target=-1;}
    if(ui.tab==0) {
        input("Arrival CSV file",ui.path);if(ImGui::Button("Select CSV..."))action(ui,[&]{if(auto path=open_arrival_csv_dialog(ui.path,ui.error))ui.path=path->string();});
        ImGui::TextWrapped("Schema: one timestamp column, YYYY-MM-DDTHH:MM:SSZ. Duplicate arrivals retained. Unsupported records reject the complete import.");
        input("Observation start (inclusive UTC)",ui.start);input("Observation end (exclusive UTC)",ui.end);
        ImGui::BeginDisabled(ui.explicit_windows);ImGui::Checkbox("I confirm continuous observation, including zero-arrival periods",&ui.continuous);ImGui::EndDisabled();
        ImGui::Checkbox("Use explicit coverage windows",&ui.explicit_windows);
        if(ui.explicit_windows) {
            input("Coverage request JSON (version 2)",ui.request_path);
            if(ImGui::Button("Load coverage definition"))action(ui,[&]{auto request=demand::load_request(ui.request_path);ui.path=request.csv.string();ui.start=demand::timestamp(request.exposure.start);ui.end=demand::timestamp(request.exposure.end);ui.split=demand::timestamp(request.fit.split);ui.model=request.fit.model==demand::Model::DailyProfile;ui.daily_bin=std::to_string(request.fit.daily_bin_seconds);ui.diagnostic_bin=std::to_string(request.fit.diagnostic_bin_seconds);ui.continuous=request.exposure.continuous;ui.explicit_windows=!request.exposure.windows.empty();ui.windows.clear();for(const auto& window:request.exposure.windows)ui.windows.push_back({demand::timestamp(window.start),demand::timestamp(window.end),demand::coverage_name(window.status)});});ui.load_request=center();
            ImGui::TextWrapped("Only observed windows fit intensity. Closed, unobserved and interrupted time is excluded. Declare every gap; historical coverage does not define the simulated operating schedule.");
            ImGui::BeginChild("Coverage window editor",{0,180},ImGuiChildFlags_Borders);
            for(std::size_t i=0;i<ui.windows.size();++i){ImGui::PushID(static_cast<int>(i));ImGui::Text("Window %zu [start,end)",i);input("Start UTC",ui.windows[i][0]);input("End UTC",ui.windows[i][1]);const char* labels[]{"observed","unobserved","closed","interrupted"};int status=0;for(int j=0;j<4;++j)if(ui.windows[i][2]==labels[j])status=j;if(ImGui::Combo("Coverage status",&status,labels,4))ui.windows[i][2]=labels[status];if(ImGui::Button("Remove coverage window")){ui.windows.erase(ui.windows.begin()+i);ImGui::PopID();break;}ImGui::Separator();ImGui::PopID();}ImGui::EndChild();ui.coverage_editor=center();
            ImGui::BeginDisabled(ui.windows.size()>=demand::max_coverage_windows);if(ImGui::Button("Add coverage window"))ui.windows.push_back({ui.windows.empty()?ui.start:ui.windows.back()[1],ui.end,"observed"});ImGui::EndDisabled();
        } else ImGui::TextWrapped("Continuous means every interval was observed, including zero arrivals. Use explicit windows for closures or collection gaps.");
        ImGui::BeginDisabled(progress.active || w.execution().progress().active());
        if(ImGui::Button("Import and validate"))action(ui,[&]{demand::ImportSettings exposure{demand::parse_timestamp(ui.start),demand::parse_timestamp(ui.end),ui.explicit_windows?false:ui.continuous};if(ui.explicit_windows)for(const auto& s:ui.windows){bool found=false;demand::CoverageStatus status{};for(auto v:{demand::CoverageStatus::Observed,demand::CoverageStatus::Unobserved,demand::CoverageStatus::Closed,demand::CoverageStatus::Interrupted})if(s[2]==demand::coverage_name(v)){status=v;found=true;}if(!found)throw std::invalid_argument("Unsupported coverage status");exposure.windows.push_back({demand::parse_timestamp(s[0]),demand::parse_timestamp(s[1]),status});}exposure=demand::normalize_exposure(std::move(exposure));w.import_calibration({std::filesystem::path(ui.path),std::move(exposure),{}});ui.preview.reset();});ui.import=center();ImGui::EndDisabled();
        if(ui.scroll_target==7){ImGui::SetScrollHereY(0.6f);ui.scroll_target=-1;}
        if(auto data=w.calibration_dataset()) {
            const auto& p=data->provenance;
            ImGui::Text("Accepted: %zu | Rejected: 0 | Simultaneous duplicates: %zu",p.accepted,p.duplicates);
            const auto observed=demand::observed_exposure(p.exposure,p.exposure.start,p.exposure.end);
            ImGui::Text("Ordering: %s | Observed: %llu s | Excluded: %llu s",p.reordered?"stably normalized":"already chronological",observed,p.exposure.end-p.exposure.start-observed);
            ImGui::TextWrapped("Validated snapshot: %s to %s. Later edits to import fields require another import.",demand::timestamp(p.exposure.start).c_str(),demand::timestamp(p.exposure.end).c_str());
            ImGui::TextWrapped("Dataset: %s\nSHA-256: %s",p.dataset_id.c_str(),p.sha256.c_str());
            std::vector<double> counts;std::vector<std::string> tips;for(std::size_t i=0;i<64;++i){const auto a=p.exposure.start+(p.exposure.end-p.exposure.start)*i/64,b=p.exposure.start+(p.exposure.end-p.exposure.start)*(i+1)/64;const auto exposure=demand::observed_exposure(p.exposure,a,b);counts.push_back(exposure?static_cast<double>(data->overview_counts[i]):NAN);tips.push_back("UTC block "+std::to_string(i)+": "+std::to_string(data->overview_counts[i])+" observed arrivals; observed exposure "+std::to_string(exposure)+" seconds; shaded cells are excluded coverage");}
            if(p.exposure.windows.empty())chart("Observed arrival counts (64 exposure blocks)","Arrivals / exposure block",counts,{},tips,demand::timestamp(p.exposure.start),demand::timestamp(p.exposure.end));
            else {counts.clear();tips.clear();std::vector<double> edges;std::vector<demand::CoverageStatus> statuses;for(std::size_t i=0;i<p.exposure.windows.size();++i){const auto& window=p.exposure.windows[i];counts.push_back(window.status==demand::CoverageStatus::Observed?static_cast<double>(data->coverage_counts[i]):NAN);edges.push_back(static_cast<double>(window.start-p.exposure.start)/static_cast<double>(p.exposure.end-p.exposure.start));statuses.push_back(window.status);tips.push_back(demand::timestamp(window.start)+" to "+demand::timestamp(window.end)+" | "+demand::coverage_name(window.status)+" | count="+std::to_string(data->coverage_counts[i]));}edges.push_back(1);chart("Arrival observations and coverage","Teal: observed counts | Grey: closed | Purple: unobserved | Amber: interrupted",counts,{},tips,demand::timestamp(p.exposure.start),demand::timestamp(p.exposure.end),-1,false,edges,statuses);}ui.chart=center();
            if(ui.scroll_target==6){ImGui::SetScrollHereY(0.9f);ui.scroll_target=-1;}
            if(!p.exposure.windows.empty() && ImGui::BeginTable("Validated coverage",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,{0,160},600)){for(auto label:{"Start UTC","End UTC","Status"})ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();ImGuiListClipper clipper;clipper.Begin(static_cast<int>(p.exposure.windows.size()));while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){const auto& window=p.exposure.windows[i];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(demand::timestamp(window.start).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(demand::timestamp(window.end).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(demand::coverage_name(window.status));}ImGui::EndTable();}
        }
    } else if(ui.tab==1) {
        if(auto data=w.calibration_dataset())ImGui::TextWrapped("Fitting immutable validated snapshot %s. CSV is not reread.",data->provenance.source_filename.c_str());else ImGui::TextWrapped("Import and validate observations before fitting.");
        input("Chronological split (UTC)",ui.split);ImGui::Combo("Manual calibration model",&ui.model,"Constant intensity\0Repeating UTC daily profile\0");
        input("Predefined UTC daily bin width (seconds)",ui.daily_bin);input("Diagnostic bin width (seconds)",ui.diagnostic_bin);
        ImGui::TextWrapped("Daily bins start at UTC midnight, aggregate actual training exposure across days, and include partial days. Width divides 86400, minimum 1800. Profile needs at least a 24-hour training span; hours without observed exposure remain unavailable. At most 1024 diagnostics. No automatic model search.");
        ImGui::BeginDisabled(progress.active || !w.calibration_dataset() || w.execution().progress().active());
        if(ImGui::Button("Fit training and evaluate holdout"))action(ui,[&]{w.fit_calibration({demand::parse_timestamp(ui.split),ui.model?demand::Model::DailyProfile:demand::Model::Constant,static_cast<std::uint32_t>(number(ui.daily_bin,86400)),static_cast<std::uint32_t>(number(ui.diagnostic_bin,demand::max_exposure_seconds))});ui.preview.reset();});ui.fit=center();ImGui::EndDisabled();
        ImGui::TextWrapped("Earlier [start,split) observations fit parameters. Later [split,end) observations only evaluate predictions. Holdout becomes model-selection data when used to compare models; it is not independent final-test confirmation.");
        if(auto r=w.calibration_report())metrics(*r);
    } else if(ui.tab==2) {
        if(auto r=w.calibration_report()) {
            ImGui::TextWrapped("%s | %zu rate parameters | Split %s",r->settings.model==demand::Model::Constant?"Constant intensity":"Repeating UTC daily intensity",r->rates.size(),demand::timestamp(r->settings.split).c_str());metrics(*r);
            std::vector<double> counts,expected,residuals,rate_values,edges;std::vector<std::string> tips,rate_tips;
            for(std::size_t i=0;i<r->rates.size();++i){const auto& rate=r->rates[i];rate_values.push_back(rate.available?rate.per_second*3600:NAN);rate_tips.push_back(!rate.available?("Rate bin "+std::to_string(i)+": unavailable; no observed training exposure. Rate and uncertainty cannot be estimated."):"Rate bin "+std::to_string(i)+(rate.available?": ":": UNAVAILABLE (no exposure); ")+std::to_string(rate.per_second*3600)+" arrivals/hour; conservative marginal 95% ["+std::to_string(rate.lower_per_second*3600)+", "+std::to_string(rate.upper_per_second*3600)+"]; training count "+std::to_string(rate.count)+" / exposure "+std::to_string(rate.exposure_seconds)+" seconds");}
            chart("Fitted demand intensity", "Arrivals/hour; conservative marginal 95% intervals in hover/table",rate_values,{},rate_tips,r->rates.size()==1?"Constant":"00:00 UTC",r->rates.size()==1?"Training count / exposure":"24:00 UTC");ui.chart=center();
            if(ui.scroll_target==1){ImGui::SetScrollHereY(0.3f);ui.scroll_target=-1;}
            for(const auto& b:r->bins){if(!b.holdout)continue;edges.push_back(static_cast<double>(b.start-r->settings.split)/static_cast<double>(r->provenance.exposure.end-r->settings.split));const auto exposure=demand::observed_exposure(r->provenance.exposure,b.start,b.end);counts.push_back(exposure?static_cast<double>(b.observed):NAN);expected.push_back(exposure && b.prediction_available?b.expected:NAN);residuals.push_back(exposure && b.prediction_available?b.residual:NAN);tips.push_back(demand::timestamp(b.start)+" to "+demand::timestamp(b.end)+" | observed="+std::to_string(b.observed)+" expected="+(b.prediction_available?std::to_string(b.expected):std::string{"unavailable"})+" residual="+(b.prediction_available?std::to_string(b.residual):std::string{"unavailable"})+" coverage="+demand::coverage_name(demand::coverage_at(r->provenance.exposure,b.start))+" prediction="+(b.prediction_available?"available":"unavailable"));}edges.push_back(1);
            chart("Holdout observed versus expected","Arrivals/bin: teal observed, amber expected; x = UTC time",counts,expected,tips,demand::timestamp(r->settings.split),demand::timestamp(r->provenance.exposure.end),-1,false,edges);
            if(ui.scroll_target==4){ImGui::SetScrollHereY(0.35f);ui.scroll_target=-1;}
            chart("Holdout residuals","Observed minus expected arrivals/bin; x = UTC time",residuals,{},tips,demand::timestamp(r->settings.split),demand::timestamp(r->provenance.exposure.end),-1,true,edges);
            counts.clear();tips.clear();edges.clear();const auto duration=r->provenance.exposure.end-r->provenance.exposure.start;
            for(const auto& b:r->bins){edges.push_back(static_cast<double>(b.start-r->provenance.exposure.start)/static_cast<double>(duration));counts.push_back(demand::observed_exposure(r->provenance.exposure,b.start,b.end)?static_cast<double>(b.observed):NAN);tips.push_back(demand::timestamp(b.start)+" to "+demand::timestamp(b.end)+" | "+(b.holdout?"holdout":"training")+" count="+std::to_string(b.observed));}edges.push_back(1);
            chart("Observed counts: training and holdout","Arrivals/bin; x = UTC time (hover for exact exposure)",counts,{},tips,demand::timestamp(r->provenance.exposure.start),demand::timestamp(r->provenance.exposure.end),static_cast<float>(r->settings.split-r->provenance.exposure.start)/static_cast<float>(duration),false,edges);
            if(ImGui::CollapsingHeader("Rates and uncertainty table"))if(ImGui::BeginTable("Demand rates",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollX,{0,0},600)){
                for(auto label:{"UTC bin","Training count","Exposure seconds","Rate/hour","Marginal 95% rate/hour"})ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
                for(std::size_t i=0;i<r->rates.size();++i){const auto& v=r->rates[i];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%zu",i);ImGui::TableNextColumn();ImGui::Text("%llu",v.count);ImGui::TableNextColumn();ImGui::Text("%llu",v.exposure_seconds);ImGui::TableNextColumn();if(v.available)ImGui::Text("%.6g",v.per_second*3600);else ImGui::TextUnformatted("Unavailable");ImGui::TableNextColumn();if(v.available)ImGui::Text("[%.6g, %.6g]",v.lower_per_second*3600,v.upper_per_second*3600);else ImGui::TextUnformatted("No exposure");}ImGui::EndTable();}
            if(ImGui::CollapsingHeader("All numerical interval diagnostics"))if(ImGui::BeginTable("Demand bins",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,{0,200},650)){
                for(auto label:{"Start UTC / partition","Exposure s","Observed","Expected","Residual"})ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();ImGuiListClipper clipper;clipper.Begin(static_cast<int>(r->bins.size()));
                while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){const auto& b=r->bins[i];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%s %s",demand::timestamp(b.start).c_str(),b.holdout?"H":"T");ImGui::TableNextColumn();ImGui::Text("%lld",static_cast<long long>(demand::observed_exposure(r->provenance.exposure,b.start,b.end)));ImGui::TableNextColumn();ImGui::Text("%llu",b.observed);ImGui::TableNextColumn();if(b.prediction_available)ImGui::Text("%.6g",b.expected);else ImGui::TextUnformatted("Unavailable");ImGui::TableNextColumn();if(b.prediction_available)ImGui::Text("%.6g",b.residual);else ImGui::TextUnformatted("Unavailable");}ImGui::EndTable();}
            for(const auto& warning:r->warnings)ImGui::TextWrapped("Diagnostic: %s",warning.c_str());
            if(ImGui::CollapsingHeader("Statistical assumptions and limitations"))for(std::size_t i=0;i<r->limitations.size();++i){if(i==5 && r->provenance.exposure.windows.empty())ImGui::TextWrapped("Generator-1 statement retained in legacy evidence: %s Current generator 2 supports seeded Poisson conversion.",r->limitations[i].c_str());else ImGui::TextWrapped("%s",r->limitations[i].c_str());}
        }else ImGui::TextWrapped("Fit a model to inspect its validation evidence.");
    } else if(auto r=w.calibration_report()) {
        ImGui::TextWrapped("Calibration %s | %s",r->id.c_str(),r->provenance.source_filename.c_str());
        if(ImGui::Button("Save calibration study"))action(ui,[&]{w.retain_calibration(r);});ui.save=center();
        ImGui::SameLine();ImGui::TextDisabled(w.has_pending_calibration()?"Unretained evidence; save before project Save":"Retained in Study History");
        if(w.has_pending_calibration() && ImGui::Button("Discard unretained calibration..."))ImGui::OpenPopup("Discard calibration?");
        if(ImGui::BeginPopupModal("Discard calibration?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Discard this unretained completed report?");if(ImGui::Button("Discard")){w.discard_calibration(true);ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Keep"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        input("Report export destination (new file)",ui.export_path);
        if(ImGui::Button("Export calibration JSON"))action(ui,[&]{demand::save_export(ui.export_path,demand::report_json(*r));});ImGui::SameLine();
        if(ImGui::Button("Export calibration CSV"))action(ui,[&]{demand::save_export(ui.export_path,demand::report_csv(*r));});
        ImGui::Separator();ImGui::TextWrapped("Preserve current source processing, resources and horizon. Choose a seeded Poisson realization or an explicitly accepted periodic approximation. Coverage gaps are not copied into the simulation schedule.");
        ImGui::BeginDisabled(ui.preview_job.valid());if(ImGui::Combo("Simulation arrival process",&ui.conversion_mode,"Periodic surrogate (approximation)\0Seeded Poisson realization\0")){ui.preview.reset();ui.accepted_periodic=false;}ui.conversion_selector=center();
        if(ui.conversion_mode==1 && ImGui::Checkbox("Explicitly exclude unestimated UTC hours from simulated schedule",&ui.exclude_unestimated)){ui.preview.reset();ui.accepted_periodic=false;}
        bool changed=input("New scenario name",ui.scenario_name);changed|=input("Explicit realization seed",ui.seed);changed|=input("Scenario UTC start (profile alignment)",ui.utc_start);if(changed){ui.preview.reset();ui.accepted_periodic=false;}
        if(ImGui::Button(ui.conversion_mode?"Preview seeded Poisson realization":"Preview periodic scenario conversion"))action(ui,[&]{ui.accepted_periodic=false;if(!ui.conversion_mode)ui.preview=demand::convert(*r,w.draft_definition(),ui.scenario_name,number(ui.seed),demand::parse_timestamp(ui.utc_start),true);else {auto source=w.draft_definition();const auto name=ui.scenario_name;const auto seed=number(ui.seed);const auto start=demand::parse_timestamp(ui.utc_start);const auto exclude=ui.exclude_unestimated;ui.preview_stop=std::stop_source{};const auto stop=ui.preview_stop.get_token();const auto observer=ui.preview_observer;auto progress=std::make_shared<RealizationProgress>();ui.preview_progress=progress;ui.preview_job=std::async(std::launch::async,[r,source=std::move(source),name,seed,start,exclude,stop,observer,progress]{simulation::ExecutionControl control{stop,[observer,progress](auto phase,auto,auto count,auto){progress->generated=count;progress->phase=phase=="Generating Poisson arrivals"?1:phase=="Building realization diagnostics"?2:0;if(observer)observer(phase);}};return demand::convert_stochastic(*r,source,name,seed,start,exclude,&control);}).share();}});ui.preview_button=center();ImGui::EndDisabled();
        if(ui.preview && ui.preview->calibration_id==r->id) {
            ImGui::TextWrapped("%s",ui.preview->semantics.c_str());
            if(ImGui::BeginTable("Reviewed conversion rates",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,{0,130})) {
                for(auto label:{"UTC rate bin","Fitted arrivals/hour","Simulated arrivals/hour"})ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();
                for(std::size_t i=0;i<ui.preview->fitted_per_second.size();++i){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%zu",i);ImGui::TableNextColumn();if(ui.preview->fitted_available.empty() || ui.preview->fitted_available[i])ImGui::Text("%.6g",ui.preview->fitted_per_second[i]*3600);else ImGui::TextUnformatted("Unavailable");ImGui::TableNextColumn();ImGui::Text("%.6g",ui.preview->represented_per_second[i]*3600);}ImGui::EndTable();
            }
            const bool poisson=scenario::workload::poisson(std::get<scenario::WarehouseConfig>(ui.preview->scenario.domain));
            if(poisson)ImGui::Text("Expected count: %.6g | Realized count: %u | Seed: %llu | Generator: 2",ui.preview->expected_count,ui.preview->realized_count,ui.preview->seed);
            else ImGui::Text("Periodic horizon arrival count: %u; seed: %llu",scenario::workload::arrival_count(ui.preview->scenario),ui.preview->seed);
            if(poisson){std::vector<double> realized,expected;std::vector<std::string> tips;for(std::size_t i=0;i<64;++i){realized.push_back(static_cast<double>(ui.preview->realized_bins[i]));expected.push_back(ui.preview->expected_bins[i]);tips.push_back("Simulated tick bin "+std::to_string(i)+" | actual seeded arrivals="+std::to_string(ui.preview->realized_bins[i])+" | model expected="+std::to_string(ui.preview->expected_bins[i]));}chart("Seeded stochastic realization versus fitted expectation","Teal: generated arrivals | Amber: expected arrivals (64 tick bins); historical observations shown in Validation",realized,expected,tips,demand::timestamp(ui.preview->utc_start),demand::timestamp(ui.preview->utc_start+ui.preview->scenario.simulation.duration_seconds));}
            ImGui::Checkbox(poisson?"Use this reviewed explicit-seed stochastic realization":"Accept periodic approximation and displayed rate changes",&ui.accepted_periodic);ui.approve=center();
            if(ui.scroll_target==2){ImGui::SetScrollHereY(0.65f);ui.scroll_target=-1;}
            input("Scenario export destination (new file)",ui.scenario_path);ImGui::BeginDisabled(!ui.accepted_periodic || progress.active);
            if(ImGui::Button("Export validated scenario"))action(ui,[&]{scenario::save_scenario(ui.scenario_path,ui.preview->scenario);w.record_calibration_conversion(*ui.preview);});
            ImGui::BeginDisabled(w.runs().size()>=workspace::max_custom_runs);
            if(ImGui::Button("Create explicit-seed playable simulation"))action(ui,[&]{w.record_calibration_conversion(*ui.preview);w.create_run(ui.preview->scenario);});ui.run=center();ImGui::EndDisabled();ImGui::EndDisabled();
            if(ui.scroll_target==3){ImGui::SetScrollHereY(0.8f);ui.scroll_target=-1;}
        }
        if(!r->conversions.empty())ImGui::Text("Retained conversion provenance: %zu scenario(s)",r->conversions.size());
    }
    if(!ui.error.empty())ImGui::TextWrapped("Action failed: %s",ui.error.c_str());ImGui::EndChild();
    ui.render_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count();++ui.rendered_frames;
}
