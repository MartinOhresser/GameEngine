#include "ge/platform/sdl/tool_export.hpp"

#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <thread>
#include <utility>

namespace ge::platform::sdl {

namespace {

/// What the export thread and the launcher share.
struct Shared {
    std::mutex mutex;
    std::vector<std::string> lines;
    bool running = false;
    bool finished = false;
    bool succeeded = false;

    void add(const std::string& line) {
        const std::scoped_lock lock(mutex);
        lines.push_back(line);
    }
};

/// A stream whose lines go to the shared state as they are completed.
class LineBuffer final : public std::streambuf {
public:
    explicit LineBuffer(std::shared_ptr<Shared> shared) : shared_(std::move(shared)) {}
    LineBuffer(const LineBuffer&) = delete;
    LineBuffer& operator=(const LineBuffer&) = delete;
    LineBuffer(LineBuffer&&) = delete;
    LineBuffer& operator=(LineBuffer&&) = delete;
    ~LineBuffer() override { flush_line(); }

private:
    int_type overflow(int_type c) override {
        if (c == traits_type::eof()) {
            return traits_type::not_eof(c);
        }
        if (c == '\n') {
            flush_line();
        } else {
            line_ += traits_type::to_char_type(c);
        }
        return c;
    }

    void flush_line() {
        if (!line_.empty()) {
            shared_->add(line_);
            line_.clear();
        }
    }

    std::shared_ptr<Shared> shared_;
    std::string line_;
};

class ToolExport final : public ExportService {
public:
    explicit ToolExport(ToolExportOptions options)
        : options_(std::move(options)), shared_(std::make_shared<Shared>()) {
        for (const tools::ExportTarget* target : tools::export_targets(options_.include_hidden)) {
            names_.push_back(target->name());
            labels_.push_back(target->label());
        }
    }

    [[nodiscard]] std::vector<std::string> targets() const override { return labels_; }

    [[nodiscard]] bool start(const std::string& manifest, std::size_t target,
                             std::string& error) override {
        if (target >= names_.size()) {
            error = "That platform does not exist.";
            return false;
        }
        const std::shared_ptr<Shared> shared = shared_;
        {
            const std::scoped_lock lock(shared->mutex);
            if (shared->running) {
                error = "An export is already going on.";
                return false;
            }
            shared->running = true;
            shared->finished = false;
            shared->lines.clear();
        }
        tools::ExportRequest request;
        request.manifest = manifest;
        request.target = names_[target];
        request.out_dir = options_.dist_dir;
        request.source_dir = options_.source_dir;
        tools::CommandRunner runner = options_.runner;
        if (!runner) {
            runner = [shared](const std::vector<std::string>& command) {
                return tools::run_process_lines(
                    command, [shared](const std::string& line) { shared->add(line); });
            };
        }
        // The thread outlives the launcher if the window is closed meanwhile: it only holds
        // what it needs, and ends with the program.
        std::thread([shared, request, runner]() {
            LineBuffer buffer(shared);
            std::ostream stream(&buffer);
            const int code = tools::export_game(request, stream, stream, runner);
            stream.flush();
            const std::scoped_lock lock(shared->mutex);
            shared->running = false;
            shared->finished = true;
            shared->succeeded = code == 0;
        }).detach();
        return true;
    }

    [[nodiscard]] ExportProgress poll() override {
        const std::scoped_lock lock(shared_->mutex);
        ExportProgress progress;
        progress.lines = std::exchange(shared_->lines, {});
        progress.running = shared_->running;
        progress.finished = shared_->finished;
        progress.succeeded = shared_->succeeded;
        shared_->finished = false;
        return progress;
    }

private:
    ToolExportOptions options_;
    std::vector<std::string> names_;
    std::vector<std::string> labels_;
    std::shared_ptr<Shared> shared_;
};

} // namespace

std::unique_ptr<ExportService> make_tool_export(const ToolExportOptions& options) {
    return std::make_unique<ToolExport>(options);
}

} // namespace ge::platform::sdl
