#pragma once
#include <simforge/workspace/scenario_workspace.hpp>
struct LanguageUI
{
    bool open_requested{false}, visible{false}, input_open{true}, edit_open{false}, approved{false}, apply_requested{false};
    int mode{1};
    std::string description, error;
    std::optional<simforge::scenario::language::Proposal> proposal;
    std::array<std::string,8> edits;
    std::array<std::string,9> editor_before;
    std::array<bool,8> edited{};
    std::string name;bool name_edited{false};
    double interpretation_us{0};
    int scroll_field{-1};
    int scroll_action{-1}; // Opt-in native walkthrough navigation.
    glm::vec2 description_input{}, interpret{}, create{}, modify{}, approve{}, apply{}, discard{}, edit{}, validate_edits{}, name_input{};
    std::array<glm::vec2,8> edit_inputs{};
    glm::vec2 modal_min{},modal_max{};
    float footer_bottom{0}; // Native layout verification uses actual rendered bounds.
    float review_height{0};
};
void draw_language(simforge::workspace::ScenarioWorkspace& workspace,LanguageUI& ui);
