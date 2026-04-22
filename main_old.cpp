#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <memory>

using namespace std;

enum class SubmissionStatus {
    ACCEPTED,
    WRONG_ANSWER,
    RUNTIME_ERROR,
    TIME_LIMIT_EXCEED
};

struct Submission {
    string problem;
    string team;
    SubmissionStatus status;
    int time;

    Submission(const string& p, const string& t, SubmissionStatus s, int tm)
        : problem(p), team(t), status(s), time(tm) {}
};

struct ProblemStatus {
    int wrong_attempts = 0;
    int solve_time = -1;  // -1 means not solved
    int frozen_submissions = 0;
    bool is_frozen = false;

    // Get display string for scoreboard
    string get_display_string() const {
        if (is_frozen) {
            if (wrong_attempts == 0) {
                return "0/" + to_string(frozen_submissions);
            } else {
                return "-" + to_string(wrong_attempts) + "/" + to_string(frozen_submissions);
            }
        } else if (solve_time != -1) {
            if (wrong_attempts == 0) {
                return "+";
            } else {
                return "+" + to_string(wrong_attempts);
            }
        } else {
            if (wrong_attempts == 0) {
                return ".";
            } else {
                return "-" + to_string(wrong_attempts);
            }
        }
    }
};

struct Team {
    string name;
    map<string, ProblemStatus> problems;  // problem -> status
    int solved_count = 0;
    int total_penalty = 0;
    vector<int> solve_times;  // for tie-breaking

    Team(const string& n) : name(n) {}

    // Update team status after a submission
    void update_submission(const string& problem, SubmissionStatus status, int time, bool is_frozen_period) {
        ProblemStatus& prob = problems[problem];
        // Save current state for comparison
        int old_solved = (prob.solve_time != -1) ? 1 : 0;
        int old_penalty = (prob.solve_time != -1) ? (20 * prob.wrong_attempts + prob.solve_time) : 0;

        if (prob.solve_time != -1) {
            // Already solved, do nothing
            return;
        }

        if (is_frozen_period && !prob.is_frozen) {
            // Enter frozen state
            prob.is_frozen = true;
        }

        if (is_frozen_period) {
            prob.frozen_submissions++;
        }

        if (status == SubmissionStatus::ACCEPTED) {
            prob.solve_time = time;
            solved_count++;
            total_penalty += 20 * prob.wrong_attempts + time;
            solve_times.push_back(time);
            sort(solve_times.rbegin(), solve_times.rend());  // descending order for easy comparison
        } else {
            prob.wrong_attempts++;
        }
    }

    // Unfreeze a problem (used during scroll)
    bool unfreeze_problem(const string& problem) {
        ProblemStatus& prob = problems[problem];
        if (!prob.is_frozen) return false;

        prob.is_frozen = false;

        // Check if we have any frozen submissions that were accepted
        // Since we don't know the actual status of frozen submissions,
        // we need to check if the problem was actually solved
        // This is handled externally by re-processing submissions

        return true;
    }
};

class ICPCManager {
private:
    map<string, unique_ptr<Team>> teams;
    vector<Submission> submissions;
    bool competition_started = false;
    bool competition_ended = false;
    bool is_frozen = false;
    int duration_time = 0;
    int problem_count = 0;
    vector<string> problem_names;
    mutable vector<Team*> cached_rankings;
    mutable bool rankings_dirty = true;

    // Get problem name from index (A, B, C, ...)
    string get_problem_name(int idx) const {
        return string(1, 'A' + idx);
    }

    // Parse submission status from string
    SubmissionStatus parse_status(const string& status) const {
        if (status == "Accepted") return SubmissionStatus::ACCEPTED;
        if (status == "Wrong_Answer") return SubmissionStatus::WRONG_ANSWER;
        if (status == "Runtime_Error") return SubmissionStatus::RUNTIME_ERROR;
        if (status == "Time_Limit_Exceed") return SubmissionStatus::TIME_LIMIT_EXCEED;
        return SubmissionStatus::WRONG_ANSWER;  // default
    }

    // Calculate rankings (with caching)
    vector<Team*> calculate_rankings() const {
        if (!rankings_dirty) {
            return cached_rankings;
        }

        cached_rankings.clear();
        for (auto& [name, team] : teams) {
            cached_rankings.push_back(team.get());
        }

        sort(cached_rankings.begin(), cached_rankings.end(), [](Team* a, Team* b) {
            // First compare by solved count
            if (a->solved_count != b->solved_count) {
                return a->solved_count > b->solved_count;
            }

            // Then by penalty time
            if (a->total_penalty != b->total_penalty) {
                return a->total_penalty < b->total_penalty;
            }

            // Then by solve times (descending order)
            int min_size = min(a->solve_times.size(), b->solve_times.size());
            for (int i = 0; i < min_size; i++) {
                if (a->solve_times[i] != b->solve_times[i]) {
                    return a->solve_times[i] < b->solve_times[i];
                }
            }
            if (a->solve_times.size() != b->solve_times.size()) {
                return a->solve_times.size() > b->solve_times.size();
            }

            // Finally by team name
            return a->name < b->name;
        });

        rankings_dirty = false;
        return cached_rankings;
    }

    // Get team ranking (1-indexed)
    int get_team_ranking(const string& team_name) {
        auto rankings = calculate_rankings();
        for (int i = 0; i < rankings.size(); i++) {
            if (rankings[i]->name == team_name) {
                return i + 1;
            }
        }
        return -1;  // not found
    }

public:
    // Add a team
    string add_team(const string& team_name) {
        if (competition_started) {
            return "[Error]Add failed: competition has started.\n";
        }
        if (teams.find(team_name) != teams.end()) {
            return "[Error]Add failed: duplicated team name.\n";
        }
        teams[team_name] = make_unique<Team>(team_name);
        return "[Info]Add successfully.\n";
    }

    // Start competition
    string start_competition(int duration, int problems) {
        if (competition_started) {
            return "[Error]Start failed: competition has started.\n";
        }
        competition_started = true;
        duration_time = duration;
        problem_count = problems;

        // Generate problem names
        for (int i = 0; i < problems; i++) {
            problem_names.push_back(get_problem_name(i));
        }

        return "[Info]Competition starts.\n";
    }

    // Submit a solution
    void submit(const string& problem, const string& team_name, const string& status_str, int time) {
        SubmissionStatus status = parse_status(status_str);
        submissions.emplace_back(problem, team_name, status, time);

        // Update team status
        Team* team = teams[team_name].get();
        bool is_frozen_period = is_frozen &&
                               find(problem_names.begin(), problem_names.end(), problem) != problem_names.end() &&
                               team->problems[problem].solve_time == -1;

        team->update_submission(problem, status, time, is_frozen_period);
        rankings_dirty = true;  // Mark rankings as dirty
    }

    // Flush scoreboard
    string flush_scoreboard() {
        return "[Info]Flush scoreboard.\n";
    }

    // Freeze scoreboard
    string freeze_scoreboard() {
        if (is_frozen) {
            return "[Error]Freeze failed: scoreboard has been frozen.\n";
        }
        is_frozen = true;
        return "[Info]Freeze scoreboard.\n";
    }

    // Scroll scoreboard
    string scroll_scoreboard() {
        if (!is_frozen) {
            return "[Error]Scroll failed: scoreboard has not been frozen.\n";
        }

        stringstream result;
        result << "[Info]Scroll scoreboard.\n";

        // First flush the scoreboard
        auto rankings_before = calculate_rankings();

        // Output scoreboard before scrolling
        for (int i = 0; i < rankings_before.size(); i++) {
            Team* team = rankings_before[i];
            result << team->name << " " << (i + 1) << " " << team->solved_count
                   << " " << team->total_penalty;

            for (const string& prob_name : problem_names) {
                result << " " << team->problems[prob_name].get_display_string();
            }
            result << "\n";
        }

        // Pre-calculate which teams have frozen problems
        vector<pair<Team*, string>> frozen_problems;
        auto current_rankings = calculate_rankings();

        // Search from bottom to top to find all frozen problems
        for (int i = current_rankings.size() - 1; i >= 0; i--) {
            Team* team = current_rankings[i];

            // Find all frozen problems for this team
            for (const string& prob_name : problem_names) {
                if (team->problems[prob_name].is_frozen) {
                    frozen_problems.push_back({team, prob_name});
                }
            }
        }

        // Process each frozen problem
        for (auto [team, prob_name] : frozen_problems) {
            // Get current rankings before unfreezing
            int old_ranking = get_team_ranking(team->name);

            // Unfreeze this problem
            team->problems[prob_name].is_frozen = false;
            rankings_dirty = true;  // Mark rankings as dirty

            // Get new rankings
            int new_ranking = get_team_ranking(team->name);

            // If ranking improved, output the change
            if (new_ranking < old_ranking) {
                // Find which team was displaced
                auto new_rankings = calculate_rankings();
                string displaced_team;
                if (new_ranking <= (int)new_rankings.size()) {
                    displaced_team = new_rankings[new_ranking - 1]->name;
                }

                result << team->name << " " << displaced_team << " "
                       << team->solved_count << " " << team->total_penalty << "\n";
            }
        }

        // Output final scoreboard after scrolling
        auto final_rankings = calculate_rankings();
        for (int i = 0; i < final_rankings.size(); i++) {
            Team* team = final_rankings[i];
            result << team->name << " " << (i + 1) << " " << team->solved_count
                   << " " << team->total_penalty;

            for (const string& prob_name : problem_names) {
                result << " " << team->problems[prob_name].get_display_string();
            }
            result << "\n";
        }

        is_frozen = false;
        return result.str();
    }

    // Query team ranking
    string query_ranking(const string& team_name) {
        if (teams.find(team_name) == teams.end()) {
            return "[Error]Query ranking failed: cannot find the team.\n";
        }

        stringstream result;
        result << "[Info]Complete query ranking.\n";
        if (is_frozen) {
            result << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
        }

        int ranking = get_team_ranking(team_name);
        result << "[" << team_name << "] NOW AT RANKING " << ranking << "\n";

        return result.str();
    }

    // Query submission
    string query_submission(const string& team_name, const string& problem, const string& status) {
        if (teams.find(team_name) == teams.end()) {
            return "[Error]Query submission failed: cannot find the team.\n";
        }

        stringstream result;
        result << "[Info]Complete query submission.\n";

        // Find matching submissions (from newest to oldest)
        for (auto it = submissions.rbegin(); it != submissions.rend(); ++it) {
            const Submission& sub = *it;

            if (sub.team != team_name) continue;
            if (problem != "ALL" && sub.problem != problem) continue;
            if (status != "ALL" &&
                (status == "Accepted" && sub.status != SubmissionStatus::ACCEPTED) &&
                (status == "Wrong_Answer" && sub.status != SubmissionStatus::WRONG_ANSWER) &&
                (status == "Runtime_Error" && sub.status != SubmissionStatus::RUNTIME_ERROR) &&
                (status == "Time_Limit_Exceed" && sub.status != SubmissionStatus::TIME_LIMIT_EXCEED)) {
                continue;
            }

            // Found a match
            string status_str;
            switch (sub.status) {
                case SubmissionStatus::ACCEPTED: status_str = "Accepted"; break;
                case SubmissionStatus::WRONG_ANSWER: status_str = "Wrong_Answer"; break;
                case SubmissionStatus::RUNTIME_ERROR: status_str = "Runtime_Error"; break;
                case SubmissionStatus::TIME_LIMIT_EXCEED: status_str = "Time_Limit_Exceed"; break;
            }

            result << sub.team << " " << sub.problem << " " << status_str << " " << sub.time << "\n";
            return result.str();
        }

        result << "Cannot find any submission.\n";
        return result.str();
    }

    // End competition
    string end_competition() {
        competition_ended = true;
        return "[Info]Competition ends.\n";
    }
};

int main() {
    ICPCManager manager;
    string line;

    while (getline(cin, line)) {
        if (line.empty()) continue;

        stringstream ss(line);
        string command;
        ss >> command;

        if (command == "ADDTEAM") {
            string team_name;
            ss >> team_name;
            cout << manager.add_team(team_name);

        } else if (command == "START") {
            string duration_str, problem_str;
            int duration, problems;
            ss >> duration_str >> duration >> problem_str >> problems;
            cout << manager.start_competition(duration, problems);

        } else if (command == "SUBMIT") {
            string problem, by_str, team_name, with_str, status, at_str;
            int time;
            ss >> problem >> by_str >> team_name >> with_str >> status >> at_str >> time;
            manager.submit(problem, team_name, status, time);

        } else if (command == "FLUSH") {
            cout << manager.flush_scoreboard();

        } else if (command == "FREEZE") {
            cout << manager.freeze_scoreboard();

        } else if (command == "SCROLL") {
            cout << manager.scroll_scoreboard();

        } else if (command == "QUERY_RANKING") {
            string team_name;
            ss >> team_name;
            cout << manager.query_ranking(team_name);

        } else if (command == "QUERY_SUBMISSION") {
            string team_name, where_str, problem_eq, status_eq;
            ss >> team_name >> where_str >> problem_eq >> status_eq;

            // Parse WHERE clause
            string problem = "ALL";
            string status = "ALL";

            size_t prob_pos = problem_eq.find("PROBLEM=");
            if (prob_pos != string::npos) {
                problem = problem_eq.substr(8);  // After "PROBLEM="
            }

            size_t status_pos = status_eq.find("STATUS=");
            if (status_pos != string::npos) {
                status = status_eq.substr(7);  // After "STATUS="
            }

            cout << manager.query_submission(team_name, problem, status);

        } else if (command == "END") {
            cout << manager.end_competition();
            break;
        }
    }

    return 0;
}