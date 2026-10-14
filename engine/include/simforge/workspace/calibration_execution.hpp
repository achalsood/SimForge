#pragma once
#include <simforge/analysis/demand_calibration.hpp>
#include <mutex>
#include <thread>
namespace simforge::workspace {
struct CalibrationProgress { bool active{false},cancelled{false}; std::string phase,error; };
// Worker owns the request and file I/O. Publication belongs to the workspace owner.
class CalibrationExecution {
public:
    using Observer=std::function<void(std::string_view)>;
    ~CalibrationExecution();
    void start(analysis::demand::FileRequest,Observer={},bool import_only=false,std::shared_ptr<const analysis::demand::Dataset> dataset={});
    void cancel();
    void shutdown();
    [[nodiscard]] CalibrationProgress progress() const;
    [[nodiscard]] std::shared_ptr<const analysis::demand::Report> take_result();
    [[nodiscard]] std::shared_ptr<const analysis::demand::Dataset> take_dataset();
private:
    mutable std::mutex mutex_;
    CalibrationProgress progress_;
    std::shared_ptr<const analysis::demand::Report> result_;
    std::shared_ptr<const analysis::demand::Dataset> dataset_;
    std::jthread worker_;
};
}
