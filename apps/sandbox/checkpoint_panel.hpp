#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
#include <glm/vec2.hpp>
struct CheckpointUI {
    bool open{false}; int source{0},resource{2},action{1},objective{0},seed_policy{0},metric{0},selected_seed{0};bool post{false};
    std::string tick{"432000"},value{"1"},count{"20"},base{"1001"},seeds,error,observed_id;
    glm::vec2 toggle{},run{},cancel{},control_playback{},intervention_playback{},metric_control{},seed_control{};
    std::array<glm::vec2,5> inputs{};
    int scroll_target{-1};
    std::optional<bool> playback_requested;std::uint64_t playback_seed{0};
    std::function<void(std::string_view,std::string_view,std::uint64_t,std::uint64_t)> observer;
};
void draw_checkpoint_setup(simforge::workspace::ScenarioWorkspace&,CheckpointUI&,float);
void draw_checkpoint_results(simforge::workspace::ScenarioWorkspace&,CheckpointUI&,float);
simforge::analysis::checkpoint::Request checkpoint_request(const simforge::workspace::ScenarioWorkspace&,const CheckpointUI&);
