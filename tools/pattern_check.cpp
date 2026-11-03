/* pattern_check — headless bake/report/export for one .anim.json file.
 *
 * Usage:
 *   pattern_check <pattern.anim.json> [--hbp <out.hbp>] [--report <out.json>]
 *
 * The default report path is the input with .anim.json replaced by
 * .report.json.  Exit status is 0 when the report is clean, 1 otherwise.
 */

#include "pattern_core/pattern_core.h"

#include <stdio.h>
#include <string>

static bool ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string default_report_path(const std::string &input) {
    const std::string anim = ".anim.json";
    if (ends_with(input, anim))
        return input.substr(0, input.size() - anim.size()) + ".report.json";
    return input + ".report.json";
}

int main(int argc, char **argv) {
    std::string input;
    std::string report_path;
    std::string hbp_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--hbp" && i + 1 < argc) {
            hbp_path = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 2;
        } else if (input.empty()) {
            input = arg;
        } else {
            fprintf(stderr, "unexpected argument: %s\n", arg.c_str());
            return 2;
        }
    }

    if (input.empty()) {
        fprintf(stderr,
                "usage: pattern_check <pattern.anim.json> "
                "[--hbp <out.hbp>] [--report <out.json>]\n");
        return 2;
    }
    if (report_path.empty())
        report_path = default_report_path(input);

    pc::AnimFile file;
    std::string error;
    if (!pc::parse_anim_file(input, &file, &error)) {
        pc::CheckResult failed;
        failed.parse_error = error;
        pc::write_report_file(report_path, failed);
        fprintf(stderr, "%s: parse error: %s\n", input.c_str(),
                error.c_str());
        fprintf(stderr, "report: %s\n", report_path.c_str());
        return 1;
    }

    pc::Trajectory traj = pc::bake(file);
    pc::CheckResult result = pc::check(file, traj);
    if (!pc::write_report_file(report_path, result)) {
        fprintf(stderr, "cannot write report: %s\n", report_path.c_str());
        return 2;
    }

    if (!result.ok()) {
        fprintf(stderr, "%s: %zu violation(s)\n", input.c_str(),
                result.violations.size());
        for (size_t i = 0; i < result.violations.size() && i < 20; ++i) {
            const pc::Violation &v = result.violations[i];
            fprintf(stderr,
                    "  ball %d t=%lld ms  %s  measured=%lld allowed=%lld\n",
                    v.ball, (long long)v.time_ms, v.limit.c_str(),
                    (long long)v.measured, (long long)v.allowed);
        }
        if (result.violations.size() > 20)
            fprintf(stderr, "  ... %zu more\n",
                    result.violations.size() - 20);
        fprintf(stderr, "report: %s\n", report_path.c_str());
        return 1;
    }

    printf("%s: clean (%d x %d, %d ms)\n", input.c_str(), file.cols,
           file.rows, file.duration_ms);
    printf("report: %s\n", report_path.c_str());

    if (!hbp_path.empty()) {
        if (!pc::write_hbp(hbp_path, file, traj, &error)) {
            fprintf(stderr, "cannot write hbp: %s\n", error.c_str());
            return 2;
        }
        printf("hbp: %s\n", hbp_path.c_str());
    }
    return 0;
}
