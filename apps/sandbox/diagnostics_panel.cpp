#include "diagnostics_panel.hpp"
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
namespace diagnostics=analysis::diagnostics;
glm::vec2 center(){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();return {(a.x+b.x)/2,(a.y+b.y)/2};}
void input(const char* label,std::string& text) {
    std::array<char,4097> buffer{};std::memcpy(buffer.data(),text.data(),std::min(text.size(),buffer.size()-1));
    ImGui::TextUnformatted(label);ImGui::PushID(label);ImGui::SetNextItemWidth(-1);if(ImGui::InputText("##value",buffer.data(),buffer.size()))text=buffer.data();ImGui::PopID();
}
template<class T>T number(const std::string& text) {
    T n{};const auto p=std::from_chars(text.data(),text.data()+text.size(),n);
    if(p.ec!=std::errc{} || p.ptr!=text.data()+text.size())throw std::invalid_argument("Enter a finite decimal number in the supported range");return n;
}
template<class F>void action(DiagnosticsUI& ui,F f){try{f();ui.error.clear();}catch(const std::exception& e){ui.error=e.what();}}
void optional(const char* label,std::optional<double> v){if(v)ImGui::Text("%s: %.6g",label,*v);else ImGui::Text("%s: unavailable",label);}
void bounds(const char* label,const std::optional<diagnostics::Interval>& v){if(v)ImGui::Text("%s: [%llu, %llu] arrivals",label,v->lower,v->upper);else ImGui::Text("%s: unavailable",label);}
void plot(const diagnostics::Report& r,int mode) {
    const char* titles[]{"Observed versus expected arrivals","Standardized residuals","Holdout predictive count intervals"};
    ImGui::TextUnformatted(mode==2 && r.request.settings.partition==diagnostics::Partition::Training?"Training predictive intervals (descriptive)":titles[mode]);
    ImGui::TextWrapped("%s",mode==0?"Teal: observed | Amber: expected | Grey: no exposure or unavailable rate; faint tint: partial observed exposure":mode==1?"Teal: Pearson residual (observed - expected) / sqrt(expected) | Grey: unavailable; gaps are not connected":"Violet: parameter-aware envelope | Teal: process-only interval | Amber: observed count");
    const auto width=std::max(180.0f,ImGui::GetContentRegionAvail().x),height=190.0f;
    ImGui::InvisibleButton(titles[mode],{width,height+50});const auto origin=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 a{origin.x+48,origin.y+8},b{origin.x+width-12,origin.y+height};
    double maximum=1;
    for(const auto& bin:r.bins){if(!bin.available)continue;if(mode==1){if(bin.residual)maximum=std::max(maximum,std::abs(*bin.residual));}else{maximum=std::max(maximum,static_cast<double>(bin.count));if(mode==0 && bin.prediction.mean)maximum=std::max(maximum,*bin.prediction.mean);if(mode==2 && bin.prediction.process)maximum=std::max(maximum,static_cast<double>(bin.prediction.process->upper));if(mode==2 && bin.prediction.parameter_aware)maximum=std::max(maximum,static_cast<double>(bin.prediction.parameter_aware->upper));}}
    const auto y=[&](double v){return mode==1?static_cast<float>((a.y+b.y)/2-v/maximum*(b.y-a.y)/2):static_cast<float>(b.y-v/maximum*(b.y-a.y));};
    draw->AddLine(a,{a.x,b.y},IM_COL32(140,150,160,255));draw->AddLine({a.x,y(0)},{b.x,y(0)},IM_COL32(140,150,160,255));
    char label[48]{};snprintf(label,sizeof(label),"%.3g",maximum);draw->AddText({origin.x,a.y},IM_COL32(190,200,210,255),label);draw->AddText({origin.x,y(0)},IM_COL32(190,200,210,255),"0");if(mode==1){snprintf(label,sizeof(label),"-%.3g",maximum);draw->AddText({origin.x,b.y-12},IM_COL32(190,200,210,255),label);}
    const auto span=static_cast<double>(r.bins.back().end-r.bins.front().start);
    for(const auto& bin:r.bins) {
        const auto left=a.x+static_cast<float>((bin.start-r.bins.front().start)/span)*(b.x-a.x),right=a.x+static_cast<float>((bin.end-r.bins.front().start)/span)*(b.x-a.x),x=(left+right)/2;
        if(!bin.available || (mode==1 && !bin.residual)){draw->AddRectFilled({left,a.y},{right,b.y},IM_COL32(80,85,95,75));continue;}
        if(!bin.fully_observed)draw->AddRectFilled({left,a.y},{right,b.y},IM_COL32(110,115,130,45));if(mode==0){draw->AddRectFilled({left+1,y(static_cast<double>(bin.count))},{std::max(left+2,right-1),b.y},IM_COL32(60,175,165,180));draw->AddLine({left,y(*bin.prediction.mean)},{right,y(*bin.prediction.mean)},IM_COL32(245,185,75,255),2);}
        else if(mode==1){draw->AddLine({x,y(0)},{x,y(*bin.residual)},IM_COL32(75,190,180,255),2);draw->AddCircleFilled({x,y(*bin.residual)},2,IM_COL32(90,210,200,255));}
        else {if(auto v=bin.prediction.parameter_aware)draw->AddLine({x,y(static_cast<double>(v->lower))},{x,y(static_cast<double>(v->upper))},IM_COL32(165,125,230,150),5);if(auto v=bin.prediction.process){draw->AddLine({x,y(static_cast<double>(v->lower))},{x,y(static_cast<double>(v->upper))},IM_COL32(80,215,195,255),2);draw->AddLine({x-3,y(static_cast<double>(v->upper))},{x+3,y(static_cast<double>(v->upper))},IM_COL32(80,215,195,255));}draw->AddCircleFilled({x,y(static_cast<double>(bin.count))},2.5f,IM_COL32(250,185,65,255));}
    }
    if(ImGui::IsItemHovered()){const auto x=ImGui::GetIO().MousePos.x;const auto time=r.bins.front().start+static_cast<std::int64_t>(std::clamp((x-a.x)/(b.x-a.x),0.0f,0.99999f)*span);for(const auto& bin:r.bins)if(time>=bin.start && time<bin.end){ImGui::BeginTooltip();ImGui::Text("%s .. %s UTC",analysis::demand::timestamp(bin.start).c_str(),analysis::demand::timestamp(bin.end).c_str());ImGui::Text("Observed %llu | observed exposure %llu seconds",bin.count,bin.exposure_seconds);optional("Expected arrivals",bin.prediction.mean);optional("Standardized residual",bin.residual);bounds("Process-only",bin.prediction.process);bounds("Parameter-aware",bin.prediction.parameter_aware);ImGui::EndTooltip();break;}}
    const auto first=analysis::demand::timestamp(r.bins.front().start),last=analysis::demand::timestamp(r.bins.back().end);draw->AddText({a.x,b.y+5},IM_COL32(190,200,210,255),first.c_str());const auto text_size=ImGui::CalcTextSize(last.c_str());draw->AddText({std::max(a.x,b.x-text_size.x),b.y+22},IM_COL32(190,200,210,255),last.c_str());
    ImGui::TextDisabled("X: UTC [start,end) bins | Y: %s | %s",mode==1?"dimensionless Pearson residual":"arrivals per observed bin",r.request.settings.partition==diagnostics::Partition::Holdout?"holdout predictive check":"training fit check (descriptive)");
}
void reference_plot(const diagnostics::Report& r,const DiagnosticsUI& ui) {
    ImGui::TextUnformatted("Dispersion reference distribution");ImGui::TextWrapped("Teal: fixed-parameter synthetic replications | Amber: observed dispersion (clamped marker when outside reference range)");
    const auto width=std::max(180.0f,ImGui::GetContentRegionAvail().x),height=160.0f;ImGui::InvisibleButton("Dispersion histogram",{width,height+30});const auto origin=ImGui::GetItemRectMin();auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 a{origin.x+42,origin.y+5},b{origin.x+width-12,origin.y+height};const auto maximum=std::max<std::uint64_t>(1,*std::max_element(ui.histogram.begin(),ui.histogram.end()));
    for(std::size_t i=0;i<32;++i){const auto left=a.x+static_cast<float>(i)/32*(b.x-a.x),right=a.x+static_cast<float>(i+1)/32*(b.x-a.x),top=b.y-static_cast<float>(ui.histogram[i])/static_cast<float>(maximum)*(b.y-a.y);draw->AddRectFilled({left+1,top},{right-1,b.y},IM_COL32(60,175,165,210));}
    if(r.observed.dispersion){const auto x=a.x+static_cast<float>(std::clamp((*r.observed.dispersion-ui.histogram_min)/(ui.histogram_max-ui.histogram_min),0.0,1.0))*(b.x-a.x);draw->AddLine({x,a.y},{x,b.y},IM_COL32(245,185,75,255),2);}
    char label[80]{};snprintf(label,sizeof(label),"%llu",maximum);draw->AddText({origin.x,a.y},IM_COL32(190,200,210,255),label);snprintf(label,sizeof(label),"%.3g",ui.histogram_min);draw->AddText({a.x,b.y+4},IM_COL32(190,200,210,255),label);snprintf(label,sizeof(label),"%.3g",ui.histogram_max);draw->AddText({b.x-55,b.y+4},IM_COL32(190,200,210,255),label);
    ImGui::TextDisabled("X: dimensionless Pearson dispersion | Y: replication count");optional("Observed dispersion",r.observed.dispersion);
    if(r.observed.dispersion && (*r.observed.dispersion<ui.histogram_min || *r.observed.dispersion>ui.histogram_max))ImGui::TextUnformatted("Observed statistic is outside the displayed synthetic reference range.");
}
}
void draw_diagnostics(simforge::workspace::ScenarioWorkspace& w,DiagnosticsUI& ui) {
    const auto began=std::chrono::steady_clock::now();using namespace simforge;namespace diagnostics=analysis::diagnostics;
    const auto report=w.diagnostic_report();const auto progress=w.calibration_execution().progress();
    if(report && ui.viewed_id!=report->id) {
        const auto& settings=report->request.settings;ui.source_id=report->request.source->id;
        ui.partition=settings.partition==diagnostics::Partition::Holdout?1:0;ui.bin_seconds=std::to_string(settings.bin_seconds);
        ui.replications=std::to_string(settings.replications);ui.seed=std::to_string(settings.seed);ui.threshold=std::to_string(settings.tail_threshold);
        ui.level=std::to_string(settings.level);ui.prediction_start=analysis::demand::timestamp(settings.prediction_start);ui.prediction_seconds=std::to_string(settings.prediction_seconds);ui.exploratory=settings.exploratory;
        ui.viewed_id=report->id;ui.title=report->title;ui.notes=report->notes;ui.histogram.fill(0);ui.histogram_min=INFINITY;ui.histogram_max=-INFINITY;
        for(const auto& rep:report->replications)if(rep.statistics.dispersion){ui.histogram_min=std::min(ui.histogram_min,*rep.statistics.dispersion);ui.histogram_max=std::max(ui.histogram_max,*rep.statistics.dispersion);}
        if(!std::isfinite(ui.histogram_min)){ui.histogram_min=0;ui.histogram_max=1;}if(ui.histogram_max<=ui.histogram_min)ui.histogram_max=ui.histogram_min+1;
        for(const auto& rep:report->replications)if(rep.statistics.dispersion)++ui.histogram[static_cast<std::size_t>(std::clamp((*rep.statistics.dispersion-ui.histogram_min)/(ui.histogram_max-ui.histogram_min)*32,0.0,31.0))];
    }
    ImGui::TextWrapped("Demand Diagnostics | Frozen calibration | Exposure-aware counts");
    if(progress.active){ImGui::Text("Phase: %s | Completed/processed: %llu / %llu",progress.phase.c_str(),progress.processed,progress.total);if(ImGui::Button("Cancel diagnostics"))w.calibration_execution().cancel();ui.cancel=center();}
    if(progress.cancelled)ImGui::TextUnformatted("Cancelled; previous completed diagnostic is preserved.");if(!progress.error.empty()){ImGui::TextWrapped("Last attempt failed; previous report is preserved.");if(ImGui::CollapsingHeader("Failure details"))ImGui::TextWrapped("%s",progress.error.c_str());}
    const char* wide_tabs[]{"Diagnostic setup","Counts & residuals","Reference checks","Predictive uncertainty","Summary & history"};const char* compact_tabs[]{"Setup","Counts / residuals","References","Prediction","Summary"};const auto* tabs=ImGui::GetContentRegionAvail().x<720?compact_tabs:wide_tabs;for(int i=0;i<5;++i){if(i && ImGui::GetItemRectMax().x+ImGui::CalcTextSize(tabs[i]).x+24<ImGui::GetWindowPos().x+ImGui::GetWindowWidth()-20)ImGui::SameLine();if(ImGui::Button(tabs[i])){ui.tab=i;ImGui::SetScrollY(0);}ui.tabs[i]=center();}ImGui::Separator();
    if(ui.tab==0) {
        auto source=w.calibration_report();
        if(ImGui::BeginCombo("Completed calibration",source?source->provenance.source_filename.c_str():"Select retained study")){for(const auto& record:w.calibrations()){const auto& r=*record.evidence;if(ImGui::Selectable((r.provenance.source_filename+" | "+r.id.substr(0,24)).c_str(),source==record.evidence))w.select_calibration(r.id);}ImGui::EndCombo();}ui.source_selector=center();source=w.calibration_report();
        if(source){if(ui.source_id!=source->id){ui.source_id=source->id;ui.prediction_start=analysis::demand::timestamp(source->provenance.exposure.end);ui.csv_path="";}
            ImGui::TextWrapped("Source: %s | %s | Training %llu arrivals / %llu observed seconds",source->id.c_str(),source->settings.model==analysis::demand::Model::Constant?"constant rate":"predefined UTC daily profile",source->training.observed,source->training.exposure_seconds);
            if(source->settings.model==analysis::demand::Model::Constant){const auto& rate=source->rates.front();ImGui::Text("Frozen intensity: %.6g arrivals/hour",rate.per_second*3600);ImGui::TextWrapped("Conservative 95%% parameter confidence interval: [%.6g, %.6g] arrivals/hour; this is not a count prediction interval.",rate.lower_per_second*3600,rate.upper_per_second*3600);}
            else if(ImGui::CollapsingHeader("Frozen UTC profile parameters")){if(ImGui::BeginTable("Frozen rates",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_ScrollX,{0,180},560)){for(auto label:{"UTC bin","Training exposure s","Arrivals/hour","95% parameter CI"})ImGui::TableSetupColumn(label);ImGui::TableHeadersRow();for(std::size_t i=0;i<source->rates.size();++i){const auto& rate=source->rates[i];ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%zu (width %u s)",i,source->settings.daily_bin_seconds);ImGui::TableNextColumn();ImGui::Text("%llu",rate.exposure_seconds);ImGui::TableNextColumn();if(rate.available)ImGui::Text("%.6g",rate.per_second*3600);else ImGui::TextUnformatted("Unavailable");ImGui::TableNextColumn();if(rate.available)ImGui::Text("[%.4g, %.4g]",rate.lower_per_second*3600,rate.upper_per_second*3600);else ImGui::TextUnformatted("Unavailable");}ImGui::EndTable();}}
            ImGui::TextWrapped("Raw timestamps are not stored in projects. Exact coarsening of retained bins is available; new bin edges require the original fingerprint-verified CSV.");
            ImGui::Combo("Diagnostic partition",&ui.partition,"Training fit check (descriptive)\0Holdout predictive check\0");input("Diagnostic bin width (seconds; 1..604800)",ui.bin_seconds);input("Predefined high-count threshold (arrivals)",ui.threshold);
            ImGui::Checkbox("Exploratory settings / repeated holdout inspection",&ui.exploratory);ImGui::Checkbox("Reimport original CSV with SHA-256 verification",&ui.reimport);if(ui.reimport){input("Original arrival CSV",ui.csv_path);if(ImGui::Button("Select original CSV"))if(auto path=open_arrival_csv_dialog(ui.csv_path,ui.error))ui.csv_path=path->string();}
            input("Nominal prediction coverage (.80.. .99)",ui.level);input("Future horizon start (UTC)",ui.prediction_start);input("Future horizon duration (seconds)",ui.prediction_seconds);
            input("Synthetic replications (1..1000)",ui.replications);input("Explicit reference seed",ui.seed);
            ImGui::TextWrapped("Checks freeze the fitted rates. Training simulations are not refitted and remain descriptive. Process and parameter uncertainty are shown separately.");
            ImGui::BeginDisabled(progress.active || w.execution().progress().active());
            if(ImGui::Button("Run demand diagnostics"))action(ui,[&]{diagnostics::Request request;request.source=source;auto& s=request.settings;s.partition=ui.partition?diagnostics::Partition::Holdout:diagnostics::Partition::Training;s.bin_seconds=number<std::uint32_t>(ui.bin_seconds);s.tail_threshold=number<std::uint32_t>(ui.threshold);s.level=number<double>(ui.level);s.prediction_start=analysis::demand::parse_timestamp(ui.prediction_start);s.prediction_seconds=number<std::uint32_t>(ui.prediction_seconds);s.replications=number<std::uint32_t>(ui.replications);s.seed=number<std::uint64_t>(ui.seed);s.exploratory=ui.exploratory;if(ui.reimport){if(ui.csv_path.empty())throw std::invalid_argument("Choose the original CSV");request.csv=std::filesystem::u8path(ui.csv_path);}w.start_diagnostics(std::move(request),ui.observer);});ui.run=center();if(ui.scroll_target==1){ImGui::SetScrollHereY(.7f);ui.scroll_target=0;}ImGui::EndDisabled();
        }else ImGui::TextWrapped("Retain a completed calibration study before running diagnostics.");
    } else if(report) {
        const auto& r=*report;ImGui::TextWrapped("%s | %s | %zu replications",r.title.c_str(),r.request.settings.partition==diagnostics::Partition::Holdout?"holdout":"training (descriptive)",r.replications.size());
        if(ui.tab==1){plot(r,0);plot(r,1);if(ui.scroll_target==5){ImGui::SetScrollHereY(.8f);ui.scroll_target=0;}ImGui::Text("Valid bins: %zu | Observed zero bins: %llu | Model expected zero bins: %.6g",r.observed.valid_bins,r.observed.zeros,r.observed.expected_zeros);}
        else if(ui.tab==2){reference_plot(r,ui);optional("Lower reference quantile",r.dispersion_reference.lower);optional("Upper reference quantile",r.dispersion_reference.upper);optional("Inclusive upper tail rank (finite MC)",r.dispersion_reference.upper_rank);optional("Upper-tail MC standard error",r.dispersion_reference.mc_standard_error);optional("Upper-tail fraction Wilson 95% lower",r.dispersion_reference.upper_tail_wilson_lower);optional("Upper-tail fraction Wilson 95% upper",r.dispersion_reference.upper_tail_wilson_upper);optional("Zero-count inclusive upper rank",r.zero_reference.upper_rank);optional("High-count inclusive upper rank",r.tail_reference.upper_rank);ImGui::Text("Zero bins observed: %llu | Expected: %.6g | High-count bins observed: %llu | Expected: %.6g",r.observed.zeros,r.observed.expected_zeros,r.observed.tail_bins,r.observed.expected_tail_bins);optional("Lag-one residual correlation (descriptive)",r.observed.lag_one);ImGui::Text("Adjacent complete observed pairs: %zu",r.observed.adjacent_pairs);ImGui::TextWrapped("Inclusive ties can make both tail ranks large. Multiple diagnostics and finite replication counts limit interpretation; no automatic model rejection.");}
        else if(ui.tab==3){ImGui::Text("Nominal marginal coverage: %.1f%%",r.request.settings.level*100);ImGui::TextWrapped("Future: %s + %u seconds; historical collection gaps do not define future operating hours.",analysis::demand::timestamp(r.request.settings.prediction_start).c_str(),r.request.settings.prediction_seconds);optional("Expected future arrivals",r.future.mean);bounds("Process-only predictive interval",r.future.process);bounds("Parameter-aware predictive envelope",r.future.parameter_aware);if(ui.scroll_target==6){ImGui::SetScrollHereY(.5f);ui.scroll_target=0;}ImGui::TextWrapped("The envelope includes conservative estimated-rate uncertainty under Poisson assumptions. It does not include model misspecification. Calibration confidence intervals describe rate parameters, not future arrival counts.");plot(r,2);for(bool parameter:{false,true}){const auto& c=parameter?r.parameter_coverage:r.process_coverage;ImGui::Text("%s: %llu/%llu inside | below %llu | above %llu | mean width %.3g",parameter?"Parameter-aware":"Process-only",c.inside,c.evaluated,c.below,c.above,c.mean_width);optional("Observed interval coverage",c.fraction);}}
        else {ImGui::TextWrapped("%s",r.assessment.c_str());optional("Pearson residual sum",r.observed.pearson);optional("Descriptive dispersion ratio",r.observed.dispersion);ImGui::Text("Positive-mean bins: %zu | Residual degrees of freedom: %lld",r.observed.positive_bins,static_cast<long long>(r.observed.residual_degrees_of_freedom));ImGui::TextWrapped("Source calibration: %s | CSV SHA-256: %s | %s",r.request.source->id.c_str(),r.request.source->provenance.sha256.c_str(),r.data_availability.c_str());
            if(ImGui::Button("Navigate to source calibration"))action(ui,[&]{w.select_calibration(r.request.source->id);ui.navigate_source=true;});ui.source_navigation=center();
            ImGui::TextDisabled("Embedded frozen source remains available if its linked history entry was deleted.");
            if(ImGui::Button("Save diagnostic study"))action(ui,[&]{w.retain_diagnostic(report);});ui.save=center();if(ui.scroll_target==2){ImGui::SetScrollHereY(.5f);ui.scroll_target=0;}
            if(w.has_pending_diagnostic() && ImGui::Button("Discard unretained diagnostic..."))ImGui::OpenPopup("Discard diagnostic?");if(ImGui::BeginPopupModal("Discard diagnostic?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Discard this unretained completed report?");if(ImGui::Button("Discard")){w.discard_diagnostic(true);ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Keep"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
            input("Diagnostic title",ui.title);input("Diagnostic notes",ui.notes);if(ImGui::Button("Save diagnostic title and notes"))action(ui,[&]{w.rename_diagnostic(r.id,ui.title,ui.notes);});ui.rename=center();if(ui.scroll_target==3){ImGui::SetScrollHereY(.5f);ui.scroll_target=0;}
            input("Diagnostic export destination (new file)",ui.export_path);if(ImGui::Button("Export diagnostic JSON"))action(ui,[&]{diagnostics::save_export(ui.export_path,diagnostics::report_json(r));});ui.export_json=center();ImGui::SameLine();if(ImGui::Button("Export diagnostic CSV"))action(ui,[&]{diagnostics::save_export(ui.export_path,diagnostics::report_csv(r));});ui.export_csv=center();if(ui.scroll_target==4){ImGui::SetScrollHereY(.5f);ui.scroll_target=0;}
            if(ImGui::CollapsingHeader("Assumptions, uncertainty and limitations",ImGuiTreeNodeFlags_DefaultOpen))for(const auto& warning:r.warnings)ImGui::TextWrapped("%s",warning.c_str());
        }
    } else ImGui::TextWrapped("Run diagnostics to inspect observed counts and reproducible reference evidence.");
    if(!ui.error.empty())ImGui::TextWrapped("Action failed: %s",ui.error.c_str());
    ui.render_us+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-began).count();++ui.rendered_frames;
}
