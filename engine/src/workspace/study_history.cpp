#include <simforge/workspace/scenario_workspace.hpp>
#include <simforge/workspace/experiment_execution.hpp>
#include "../scenario/json_io.hpp"
#include "../analysis/comparison_json.hpp"
#include <algorithm>
#include <chrono>

namespace simforge::workspace {
// The project codec is also the canonical bounded evidence codec.
std::string comparison_storage_json(const Comparison&);
std::shared_ptr<const Comparison> parse_comparison_storage_json(std::string_view,std::uint64_t);
const char* study_type_name(StudyType t) {
    switch(t) {
    case StudyType::SingleSeed:return "single_seed";
    case StudyType::Statistical:return "paired_statistical";
    case StudyType::Sensitivity:return "sensitivity";
    case StudyType::Interaction:return "interaction";
    case StudyType::Confirmation:return "confirmation";
    }throw std::invalid_argument("Unknown study type");
}
StudyType study_type(const Comparison& r) {
    if(int(bool(r.statistics))+int(bool(r.sensitivity))+int(bool(r.interaction))>1 || (r.confirmation && !r.statistics))
        throw std::invalid_argument("Invalid tagged study evidence");
    return r.confirmation?StudyType::Confirmation:r.interaction?StudyType::Interaction:r.sensitivity?StudyType::Sensitivity:r.statistics?StudyType::Statistical:StudyType::SingleSeed;
}
const char* study_type_label(StudyType t) {
    switch(t) {
    case StudyType::SingleSeed:return "Single-seed comparison";
    case StudyType::Statistical:return "Paired comparison";
    case StudyType::Sensitivity:return "Sensitivity sweep";
    case StudyType::Interaction:return "Interaction grid";
    case StudyType::Confirmation:return "Independent confirmation";
    }throw std::invalid_argument("Unknown study type");
}
namespace {
void metadata(const std::string& s,std::size_t maximum,bool required=false) {
    if(s.size()>maximum || s.find('\0')!=std::string::npos || (required && s.empty())) throw std::invalid_argument("Invalid study title or notes");
}
}
std::size_t ScenarioWorkspace::history_bytes() const noexcept {
    std::size_t bytes=0;for(const auto& s:studies_) {bytes+=s.serialized_bytes+s.title.size()+s.notes.size()+512;for(const auto& seeds:s.reserved_confirmation_seeds)bytes+=seeds.size()*21+32;}return bytes;
}
void ScenarioWorkspace::publish_study(std::shared_ptr<const Comparison> evidence,std::string title) {
    if(studies_.size()>=max_studies) throw std::length_error("History limit: 32 studies. Explicitly delete a study, then retain the pending result.");
    const auto type=study_type(*evidence);if(title.empty()) title=std::string(study_type_label(type))+" "+std::to_string(next_study_id_);
    if(type==StudyType::SingleSeed && (evidence->executions.empty() || evidence->definitions.empty()))throw std::invalid_argument("Empty completed study");
    if(evidence->confirmation) {
        const auto& c=*evidence->confirmation;analysis::validate_confirmation_request(c);
        auto source=std::find_if(studies_.begin(),studies_.end(),[&](const auto& s){return s.id==c.source_study_id;});
        for(const auto& s:studies_)if(s.evidence->confirmation && s.evidence->confirmation->source_study_id==c.source_study_id && s.evidence->confirmation->confirmation_number==c.confirmation_number)throw std::invalid_argument("This confirmation attempt already has a completed study");
        if(source==studies_.end() || c.confirmation_number>source->reserved_confirmation_seeds.size() || source->reserved_confirmation_seeds[c.confirmation_number-1]!=analysis::replication_seeds(c.paired.plan))throw std::invalid_argument("Confirmation has no matching reserved seed set");
        ScenarioWorkspace original;original.studies_={*source};original.studies_[0].reserved_confirmation_seeds.clear();original.selected_study_=source->id;original.report_=source->evidence;
        auto expected=original.freeze_confirmation(c.selection_id,c.paired.plan);expected.paired.id=c.paired.id;expected.source_title=c.source_title;expected.source_completed=c.source_completed;expected.confirmation_number=c.confirmation_number;expected.additional_analysis=c.additional_analysis;
        if(analysis::serialize_confirmation_request(expected)!=analysis::serialize_confirmation_request(c))throw std::invalid_argument("Confirmation evidence differs from frozen exploratory selection");
    }
    metadata(title,256,true);
    const auto bytes=comparison_storage_json(*evidence);
    // Report parsers check raw observations, aggregate consistency and provenance without rerunning simulations.
    if(bytes.size()>max_study_bytes) throw std::length_error("Study exceeds 32 MiB evidence limit; existing history retained.");
    (void)parse_comparison_storage_json(bytes,next_id_);
    const auto observations=evidence->interaction?evidence->interaction->replications.size()*(evidence->interaction->cells.size()+1):
        evidence->sensitivity?evidence->sensitivity->replications.size()*(evidence->sensitivity->points.size()+1):
        evidence->statistics?evidence->statistics->replications.size()*(evidence->statistics->candidates.size()+4):evidence->executions.size();
    if(observations>max_study_observations) throw std::length_error("Study exceeds 10,100 observation limit");
    if(history_bytes()+bytes.size()+title.size()+512>max_history_bytes) throw std::length_error("History exceeds 192 MiB. Explicitly delete a study, then retain the pending result.");
    if(next_study_id_==UINT64_MAX) throw std::overflow_error("Study identities exhausted");
    std::string id="study-"+std::to_string(next_study_id_);
    const auto completed=std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    StudyRecord record{id,std::move(title),{},completed,type,evidence,bytes.size(),{}};
    // Allocate before modifying collection/selection; shared evidence is never copied during switching.
    studies_.reserve(studies_.size()+1);studies_.push_back(std::move(record));
    ++next_study_id_;selected_study_.swap(id);report_=std::move(evidence);++persistent_generation_;++report_generation_;status_=Status::Results;
}
void ScenarioWorkspace::retain_study(Comparison r,std::string title) {publish_study(std::make_shared<const Comparison>(std::move(r)),std::move(title));}
bool ScenarioWorkspace::retain_pending_study() {
    if(!pending_study_)return false;publish_study(pending_study_);pending_study_.reset();message_="Completed study retained.";return true;
}
void ScenarioWorkspace::discard_pending_study(bool confirmed) {
    if(!confirmed)throw std::invalid_argument("Confirm discarding the unretained completed result");pending_study_.reset();
}
void ScenarioWorkspace::export_pending_json(const std::filesystem::path& path) {
    if(!pending_study_)throw std::logic_error("No pending completed study");analysis::detail::save_output(path,comparison_json(*pending_study_));
}
void ScenarioWorkspace::select_study(std::string_view id) {
    const auto found=std::find_if(studies_.begin(),studies_.end(),[&](const auto& s){return s.id==id;});
    if(found==studies_.end())throw std::invalid_argument("Unknown study ID");
    auto selected=found->id;selected_study_.swap(selected);report_=found->evidence;++persistent_generation_;++report_generation_;
    preview_view_.reset();viewed_identity_.clear();status_=Status::Results;
}
void ScenarioWorkspace::rename_study(std::string_view id,std::string title,std::string notes) {
    metadata(title,256,true);metadata(notes,4096);
    auto found=std::find_if(studies_.begin(),studies_.end(),[&](const auto& s){return s.id==id;});if(found==studies_.end())throw std::invalid_argument("Unknown study");
    if(history_bytes()-found->title.size()-found->notes.size()+title.size()+notes.size()>max_history_bytes)throw std::length_error("History metadata exceeds 192 MiB");
    found->title.swap(title);found->notes.swap(notes);++persistent_generation_;
}
void ScenarioWorkspace::delete_study(std::string_view id,bool confirmed) {
    if(!confirmed)throw std::invalid_argument("Confirm permanent study deletion");
    if(std::none_of(studies_.begin(),studies_.end(),[&](const auto& s){return s.id==id;}))throw std::invalid_argument("Unknown study");
    for(const auto& s:studies_)if(s.evidence->confirmation && s.evidence->confirmation->source_study_id==id)throw std::invalid_argument("Delete linked confirmation studies explicitly before deleting their source");
    if(pending_study_ && pending_study_->confirmation && pending_study_->confirmation->source_study_id==id)throw std::invalid_argument("Resolve pending confirmation before deleting its source");
    if(execution_->progress().active())throw std::logic_error("Wait for or cancel the active experiment before deleting studies");
    const bool selected=selected_study_==id;std::erase_if(studies_,[&](const auto& s){return s.id==id;});++persistent_generation_;
    if(selected) {selected_study_.clear();report_.reset();preview_view_.reset();++report_generation_;if(!studies_.empty())select_study(studies_.back().id);}
}
void ScenarioWorkspace::study_view(StudyViewState view) {
    if(view.metric<0 || view.metric>=int(analysis::statistical_metric_count))view.metric=0;
    for(auto& s:studies_)if(s.id==selected_study_ && s.view!=view){s.view=view;++persistent_generation_;}
}
analysis::ConfirmationRequest ScenarioWorkspace::freeze_confirmation(std::uint64_t selection,const analysis::ReplicationPlan& plan) const {
    if(!report_)throw std::logic_error("Select a completed exploratory study");const auto& r=*report_;
    if(r.confirmation)throw std::invalid_argument("Confirmation outcomes cannot be relabeled as untouched exploration");
    const auto s=std::find_if(studies_.begin(),studies_.end(),[&](const auto& v){return v.id==selected_study_;});
    if(s==studies_.end())throw std::logic_error("Exploratory study is not retained");
    analysis::ConfirmationRequest c;c.source_study_id=s->id;c.source_type=study_type_name(s->type);c.source_title=s->title;c.source_completed=s->completed;c.source_request_id=r.request_id;
    c.selection_id=selection;c.paired.baseline=r.baseline_definition;c.paired.plan=plan;c.paired.objective=r.objective;
    scenario::ScenarioDefinition candidate;
    if(r.interaction) {
        const auto& v=*r.interaction;if(v.request.x>analysis::SweepParameter::LoadingBays || v.request.y>analysis::SweepParameter::LoadingBays)throw std::invalid_argument("Workload-factor confirmation is unsupported");
        const auto p=std::find_if(v.cells.begin(),v.cells.end(),[&](const auto& x){return x.id==selection;});if(p==v.cells.end())throw std::invalid_argument("Select a tested interaction cell");
        c.objective=v.request.objective;c.exploratory_seeds=analysis::replication_seeds(v.request.plan);candidate=analysis::tested_definition(v.request,p->x,p->y,v.request.source.simulation.random_seed);
        c.exploratory_difference=p->effects[static_cast<std::size_t>(c.objective)].mean_difference;
    } else if(r.sensitivity) {
        const auto& v=*r.sensitivity;if(v.request.parameter>analysis::SweepParameter::LoadingBays)throw std::invalid_argument("Workload-factor confirmation is unsupported");
        const auto p=std::find_if(v.points.begin(),v.points.end(),[&](const auto& x){return x.id==selection;});if(p==v.points.end())throw std::invalid_argument("Select a tested sensitivity point");
        c.objective=v.request.objective;c.exploratory_seeds=analysis::replication_seeds(v.request.plan);candidate=analysis::tested_definition(v.request,p->value,v.request.source.simulation.random_seed);
        c.exploratory_difference=p->against_baseline[static_cast<std::size_t>(c.objective)].mean_difference;
    } else if(r.statistics) {
        const auto& v=*r.statistics;auto p=std::find_if(v.request.candidates.begin(),v.request.candidates.end(),[&](const auto& x){return x.id==selection;});if(p==v.request.candidates.end())throw std::invalid_argument("Select a tested candidate");
        candidate=p->definition;c.objective=analysis::objective_metric(v.request.objective);c.exploratory_seeds=analysis::replication_seeds(v.request.plan);
        const auto result=std::find_if(v.candidates.begin(),v.candidates.end(),[&](const auto& x){return x.candidate_id==selection;});c.exploratory_difference=result->metrics[static_cast<std::size_t>(c.objective)].mean_difference;
    } else {
        if(r.mode!=ComparisonMode::Initial)throw std::invalid_argument("Historical checkpoint confirmation is unsupported");
        auto p=std::find(r.candidate_ids.begin(),r.candidate_ids.end(),selection);if(p==r.candidate_ids.end())throw std::invalid_argument("Select a tested candidate");candidate=r.definitions[static_cast<std::size_t>(p-r.candidate_ids.begin())];
        if(candidate.simulation.random_seed!=r.baseline_definition.simulation.random_seed)throw std::invalid_argument("Single-seed source does not have matched exploratory seeds");
        c.objective=analysis::objective_metric(r.objective);c.exploratory_seeds={r.baseline_definition.simulation.random_seed};
        for(const auto& definition:r.definitions)if(std::find(c.exploratory_seeds.begin(),c.exploratory_seeds.end(),definition.simulation.random_seed)==c.exploratory_seeds.end())c.exploratory_seeds.push_back(definition.simulation.random_seed);
        const auto i=static_cast<std::size_t>(p-r.candidate_ids.begin());auto b=analysis::metric_value(r.baseline.metrics,c.objective),v=analysis::metric_value(r.candidates[i].metrics,c.objective);if(b&&v)c.exploratory_difference=*v-*b;
    }
    c.paired.candidates={{selection,candidate}};c.changed_inputs=analysis::confirmation_changes(c);
    if(s->reserved_confirmation_seeds.size()>=32)throw std::length_error("At most 32 confirmation attempts per source study");
    c.confirmation_number=static_cast<std::uint32_t>(s->reserved_confirmation_seeds.size()+1);c.additional_analysis=c.confirmation_number>1;
    for(const auto& set:s->reserved_confirmation_seeds)for(auto seed:set)for(auto proposed:analysis::replication_seeds(plan))if(seed==proposed)throw std::invalid_argument("Seed reserved by a previous confirmation attempt linked to this source");
    analysis::validate_confirmation_request(c);return c;
}
void ScenarioWorkspace::start_confirmation(analysis::ConfirmationRequest c,
    std::function<void(std::string_view,std::string_view,std::uint64_t,std::uint64_t)> observer) {
    if(pending_study_)throw std::logic_error("Resolve the pending completed study before starting another experiment");
    const auto s=std::find_if(studies_.begin(),studies_.end(),[&](const auto& x){return x.id==c.source_study_id;});if(s==studies_.end())throw std::invalid_argument("Confirmation source no longer retained");
    // Validate against authoritative source even if the editor or selected study has since changed.
    ScenarioWorkspace source;source.studies_=studies_;source.selected_study_=s->id;source.report_=s->evidence;
    auto expected=source.freeze_confirmation(c.selection_id,c.paired.plan);
    expected.source_title=c.source_title;expected.source_completed=c.source_completed;
    if(analysis::serialize_confirmation_request(expected)!=analysis::serialize_confirmation_request(c))throw std::invalid_argument("Frozen selection/provenance differs from source");
    const auto seed_set=analysis::replication_seeds(c.paired.plan);auto reservations=s->reserved_confirmation_seeds;reservations.push_back(seed_set);
    if(history_bytes()+seed_set.size()*21+32>max_history_bytes)throw std::length_error("Seed reservation exceeds history byte limit");
    ExperimentRequest request;request.baseline=c.paired.baseline;request.confirmation=std::move(c);execution_->start(std::move(request),std::move(observer));
    auto mutable_source=std::find_if(studies_.begin(),studies_.end(),[&](const auto& x){return x.id==s->id;});mutable_source->reserved_confirmation_seeds.swap(reservations);++persistent_generation_;
}
std::string ScenarioWorkspace::history_json() const {
    using scenario::detail::Json;Json j{{"study_history_version",1},{"selected_study",selected_study_},{"studies",Json::array()}};
    for(const auto& s:studies_)j["studies"].push_back({{"id",s.id},{"type",study_type_name(s.type)},{"title",s.title},{"notes",s.notes},{"completed",s.completed},
        {"engine",analysis::confirmation_engine_version},{"generator_version",scenario::workload_generator_version},{"reserved_confirmation_seeds",s.reserved_confirmation_seeds},{"evidence",Json::parse(comparison_storage_json(*s.evidence))}});
    auto bytes=j.dump();if(bytes.size()>max_history_bytes)throw std::length_error("History export exceeds 192 MiB");return bytes;
}
void ScenarioWorkspace::export_history_json(const std::filesystem::path& path) {analysis::detail::save_output(path,history_json());}
void ScenarioWorkspace::export_study_json(const std::filesystem::path& path) {
    const auto s=std::find_if(studies_.begin(),studies_.end(),[&](const auto& record){return record.id==selected_study_;});if(s==studies_.end())throw std::logic_error("Select a retained study");
    using scenario::detail::Json;Json j{{"study_record_version",1},{"id",s->id},{"type",study_type_name(s->type)},{"title",s->title},{"notes",s->notes},{"completed",s->completed},
        {"engine",analysis::confirmation_engine_version},{"generator_version",scenario::workload_generator_version},{"reserved_confirmation_seeds",s->reserved_confirmation_seeds},{"evidence",Json::parse(comparison_storage_json(*s->evidence))}};
    auto bytes=j.dump();if(bytes.size()>max_study_bytes+128*1024)throw std::length_error("Study export exceeds evidence/metadata limits");analysis::detail::save_output(path,bytes);
}
}
