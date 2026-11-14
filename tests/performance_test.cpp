#include "performance.hpp"
#include <iostream>
#include <stdexcept>

int main()
{
    try {
        const auto require = [](bool condition) {
            if (condition == false) {
                throw std::runtime_error("performance capture invariant failed");
            }
        };
        const auto summary = gengine::Summarize("known", {1, 9, 2, 8, 3, 7, 4, 6, 5, 10});
        require(summary.mean_ns == 5.5 && summary.p50_ns == 5 && summary.p95_ns == 10 && summary.p99_ns == 10);
        require(gengine::Summarize("empty", {}).samples == 0);
        auto capture = gengine::PerformanceCapture{};
        auto frame = gengine::FrameTimes{};
        frame.fill(42);
        capture.Start({2, 3}, "test", "2d");
        const auto reserved = capture.reserved_bytes();
        capture.Frame(frame, 1, true);
        capture.Frame(frame, 2, true);
        capture.Frame(frame, 2, true);
        capture.Host(gengine::Timing::kEditor, 12);
        capture.Host(gengine::Timing::kSchedulerLateness, 17);
        capture.Host(gengine::Timing::kPresentCpu, 23);
        capture.Host(gengine::Timing::kPresentationInterval, 16666667);
        capture.Frame(frame, 3, true);
        capture.Frame(frame, 0, false);
        capture.Frame(frame, 4, true);
        const auto report = capture.Report();
        require(report.state == "Complete" && report.collected == 3 && report.warmed == 2);
        require(report.timings[0].samples == 3 && report.timings[7].samples == 1 && report.timings[9].samples == 1);
        require(capture.reserved_bytes() == reserved && report.process_end.private_bytes > 0);
        require(report.timings[11].samples == 1 && report.timings[11].p95_ns == 17 && report.timings[12].p95_ns == 23 &&
                report.timings[13].samples == 1);
        try {
            capture.Start({0, 3601}, "test", "2d");
            require(false);
        } catch (const std::invalid_argument&) {
        }
        require(capture.Report().collected == 3);
        capture.Start({0, 10}, "test", "3d");
        capture.Frame(frame, 1, true);
        capture.Stop();
        require(capture.Report().state == "Stopped" && capture.Report().collected == 1);
        capture.Start({0, 2}, "test", "2d");
        capture.Frame(frame, 8, true, 10);
        capture.Frame(frame, 10, true, 11);
        require(capture.Report().timings[7].samples == 1);
        std::cout << "percentiles, bounded samples, GPU deduplication, partial stop, invalid start passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
