/*
 * This file is part of JCoz.
 *
 * JCoz is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * JCoz is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with JCoz.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file has been modified from lightweight-java-profiler
 * (https://github.com/dcapwell/lightweight-java-profiler). See APACHE_LICENSE for
 * a copy of the license that was included with that original work.
 */

#include "profiler.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <set>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <jvmti.h>
#include <pthread.h>
#include <algorithm>
#include <iostream>
#include <unistd.h>
#include <climits>
#include <string>
#include <sstream>

#include "display.h"
#include "globals.h"

#ifdef __APPLE__
// See comment in Accessors class
pthread_key_t Accessors::key_;
#else
__thread JNIEnv * Accessors::env_;
#endif

#define SIGNAL_FREQ 1000000L
#define MIN_EXP_TIME 5000

#define NUM_CALL_FRAMES 200
#define MAX_BCI 65536

#define SAMPLES_BATCH_SIZE 10

typedef std::chrono::duration<int, std::milli> milliseconds_type;
typedef std::chrono::duration<long, std::nano> nanoseconds_type;

ASGCTType Asgct::asgct_;

thread_local struct UserThread *curr_ut;

// Initialize static Profiler variables here
std::unordered_set<void *> Profiler::in_scope_ids;
volatile bool Profiler::in_experiment = false;
volatile pthread_t Profiler::in_scope_lock = 0;
volatile int Profiler::frame_lock = 0;
volatile int Profiler::user_threads_lock = 0;
std::vector<JVMPI_CallFrame> Profiler::call_frames;
struct Experiment Profiler::current_experiment;
std::unordered_set<struct UserThread *> Profiler::user_threads;
jvmtiEnv *Profiler::jvmti;
std::atomic<long> Profiler::global_delay(0);
std::atomic_ulong Profiler::points_hit(0);
std::atomic_bool Profiler::_running(false);
volatile bool Profiler::end_to_end = false;
pthread_t Profiler::agent_pthread;
std::atomic_bool Profiler::profile_done(false);
unsigned long Profiler::experiment_time = MIN_EXP_TIME;

// How long should we wait before starting an experiment
unsigned long Profiler::warmup_time = 5000000;
bool Profiler::prof_ready = false;

// Progress point stuff
std::string Profiler::package;
struct ProgressPoint* Profiler::progress_point = nullptr;
std::string Profiler::progress_class;
std::map<jmethodID, std::map<jint, bci_hits::hit_freq_t>> bci_hits::_freqs;
std::map<jmethodID, char*> bci_hits::_declaring_classes;
std::unordered_set<jmethodID> Profiler::prohibited_methods;

static std::atomic<int> call_index(0);
static JVMPI_CallFrame static_call_frames[NUM_CALL_FRAMES];

bool Profiler::fix_exp = false;
bool Profiler::print_traces = false;

nanoseconds_type startup_time;

// Logger
std::shared_ptr<spdlog::logger> Profiler::console_logger = spdlog::stdout_logger_mt("jcoz");
std::shared_ptr<spdlog::logger> Profiler::jcoz_logger = spdlog::basic_logger_mt("jcoz-output", "output.coz");

std::vector<std::string> Profiler::scopes_to_ignore;
JVMPI_CallFrame frame_buffer[kMaxStackTraces][kMaxFramesToCapture];
static candidate_trace traces[kMaxStackTraces];
static uint16_t trace_idx = 0;

StackTracesPrinter* printer;
FILE *traces_file;

void Profiler::ParseOptions(const char *options) {

    if( options == nullptr ) {
        fprintf(stderr, "Missing options\n");
        print_usage();
        exit(1);
    }else{
        console_logger->info("Received options: {}", options);
    }
    std::string options_str(options);
    std::stringstream ss(options_str);
    std::string item;
    std::vector<std::string> cmd_line_options;
    progress_point = new ProgressPoint();
    progress_point->lineno = -1;
    progress_point->method_id = nullptr;

    // split underscore delimited line into options
    // (we can't use semicolon because bash is dumb)
    while( std::getline(ss, item, '_') ) {
        cmd_line_options.push_back(item);
    }

    for(auto & cmd_line_option : cmd_line_options) {
        size_t equal_index = cmd_line_option.find('=');
        std::string option = cmd_line_option.substr(0, equal_index);
        std::string value = cmd_line_option.substr(equal_index + 1);

        // extract package
        if( option == "pkg" || option == "package" ) {
            Profiler::package = value;
            canonicalize(Profiler::package);
        } else if (option == "progress-point") {

            // else extract progress point
            size_t colon_index = value.find(':');
            if( colon_index == std::string::npos ) {
                fprintf(stderr, "Missing progress point\n");
                print_usage();
                exit(1);
            }

            Profiler::progress_class = value.substr(0, colon_index);
            canonicalize(Profiler::progress_class);
            progress_point->lineno = std::stoi(value.substr(colon_index + 1));

        } else if (option == "end-to-end") {
            end_to_end = true;
        } else if (option == "warmup") {
            // We expect # of milliseconds so multiply by 1000 for usleep (takes microseconds)
            warmup_time = std::stol(value) * 1000;
        } else if (option == "fix-exp" ) {
            fix_exp = true;
        } else if (option == "ignore") {
            std::stringstream ignore_scopes_stream(value);
            while( std::getline(ignore_scopes_stream, item, '|') ) {
                canonicalize(item);
                Profiler::addScopeToIgnore(item);
            }
        } else if (option == "traces") {
            print_traces = true;
        }
    }


    std::string joint_scopes;
    const char* const delim = ", ";
    for (auto next_scope = Profiler::scopes_to_ignore.begin(); next_scope != Profiler::scopes_to_ignore.end(); ++next_scope)
    {
        joint_scopes += *next_scope;
        if (next_scope != Profiler::scopes_to_ignore.end() - 1)
        {
            joint_scopes += delim;
        }
    }

    console_logger->info("Profiler arguments:\n"
                         "\tprogress point: {}:{}\n"
                         "\tscope: {}\n"
                         "\tscopes to ignore: {}\n"
                         "\twarmup: {}us\n"
                         "\tend-to-end: {}\n"
                         "\tfixed experiment duration: {}\n"
                         "\tprint stacktraces: {}",
            progress_class, progress_point->lineno, Profiler::package, joint_scopes, warmup_time, end_to_end, fix_exp, print_traces);
    if( Profiler::package.empty() || (!end_to_end && (progress_class.empty() || progress_point->lineno == -1)) ) {
        fprintf(stderr, "Missing package, progress class, or progress point\n");
        print_usage();
        exit(1);
    }
}

/**
 * Wrapper function for sleeping
 */
inline long jcoz_sleep(long nanoseconds) {

  if( nanoseconds == 0L ) {
    return 0L;
  }

  struct timespec temp_rem, temp_req;
  memset(&temp_rem, 0, sizeof(struct timespec));
  memset(&temp_req, 0, sizeof(struct timespec));
  temp_req.tv_nsec = nanoseconds;

  auto start = std::chrono::high_resolution_clock::now();

  int err = -1;
  do {
    err = nanosleep(&temp_req, &temp_rem);
    temp_req.tv_nsec = temp_rem.tv_nsec;

  } while (err == -1);

  auto end = std::chrono::high_resolution_clock::now();
  nanoseconds_type total_sleep = (end - start);

  return total_sleep.count();
}

jvmtiEnv * Profiler::getJVMTI(){
  return jvmti_;
}

bool Profiler::isRunning(){
  return _running;
}

void Profiler::signal_user_threads() {
  while (!__sync_bool_compare_and_swap(&user_threads_lock, 0, 1))
    ;
  std::atomic_thread_fence(std::memory_order_acquire);
  for (auto i = user_threads.begin(); i != user_threads.end(); i++) {
    pthread_kill((*i)->thread, SIGPROF);
  }
  user_threads_lock = 0;
  std::atomic_thread_fence(std::memory_order_release);
}

void Profiler::print_usage() {
  std::cout
    << "usage: java -agentpath:<absolute_path_to_agent>="
    << "pkg=<package_name>_"
    << "progress-point=<class:line_no>_"
    << "end-to-end (optional)_"
    << "warmup=<warmup_time_ms> (optional - default 5000 ms)"
    << "slow-exp (optional - perform exponential slowdown of experiment time with low delta)"
    << std::endl;
}

/**
 * Return random number from 0 to 1.0 in
 * increments of .05
 */
float Profiler::calculate_random_speedup() {

  int randVal = rand() % 40;

  if (randVal < 10) {
    return 0;
  } else {
    randVal = rand() % 20;

    // Number from 0 to 1.0, increments of .05
    unsigned int zeroToHundred = (randVal + 1) * 5;

    return (float) zeroToHundred / 100.f;
  }
}

void Profiler::runExperiment(JNIEnv * jni_env) {
  console_logger->info("Running experiment");
  console_logger->flush();
  in_experiment = true;
  points_hit = 0;
  for (auto user_thread : user_threads)
  {
    user_thread->local_delay = 0;
  }

  current_experiment.speedup = calculate_random_speedup();
  current_experiment.delay =
    (long) (current_experiment.speedup * SIGNAL_FREQ);

  milliseconds_type duration(experiment_time);
  auto start = std::chrono::high_resolution_clock::now();
  auto end = start + duration;
  while (_running
      && ((end_to_end && (points_hit == 0))
        || (std::chrono::high_resolution_clock::now() < end))) {
    jcoz_sleep(SIGNAL_FREQ);

    signal_user_threads();
  }

  jcoz_sleep(SIGNAL_FREQ);
  in_experiment = false;

  // Wait until all threads handle their samples
  while (true)
  {
    signal_user_threads();
    jcoz_sleep(SIGNAL_FREQ);
    bool has_not_signaled_thread = false;
    for (auto user_thread : user_threads)
    {
      if (user_thread->local_delay > 0)
      {
        console_logger->info("Thread {} had not still handled its samples. Local delay: {}",
                             reinterpret_cast<void*>(&(user_thread->java_thread)), user_thread->local_delay);
        has_not_signaled_thread = true;
        break;
      }
    }
    if (!has_not_signaled_thread) break;
  }

  //TODO this is to avoid calling up to a synchronized java method, resulting in a deadlock,
  // this might still be a race condition with Stop()
  if(!_running){
    delete[] current_experiment.location_ranges;
    return;
  }

  auto expEnd = std::chrono::high_resolution_clock::now();
  current_experiment.delay = global_delay;
  current_experiment.points_hit = points_hit;
  points_hit = 0;
  current_experiment.duration = (expEnd - start).count();
  global_delay = 0;

  char *sig = getClassFromMethodIDLocation(current_experiment.method_id);
  // throw out bad samples
  if( sig == nullptr ) return;
  cleanSignature(sig);

  // printf("Total experiment delay: %ld, total duration: %ld\n", current_experiment.delay, current_experiment.duration);

  // Maybe update the experiment length
  if (!fix_exp) {
    if( current_experiment.points_hit <= 5 ) {
      experiment_time *= 2;
    } else if( (experiment_time > MIN_EXP_TIME) && (current_experiment.points_hit >= 20) ) {
      experiment_time /= 2;
    }
  }

  bci_hits::add_hit(sig, current_experiment.method_id, current_experiment.lineno, current_experiment.bci);

  // Log the run experiment results
  console_logger->info(
                  "Ran experiment: [class: {class}:{line_no}] [speedup: {speedup}] [points hit: {points_hit}] [delay: {delay}] [duration: {duration}] [new exp time: {exp_time}]",
                  fmt::arg("exp_time", experiment_time), fmt::arg("speedup", current_experiment.speedup), fmt::arg("points_hit", current_experiment.points_hit),
                  fmt::arg("delay", current_experiment.delay), fmt::arg("duration", current_experiment.duration), fmt::arg("class", sig),
                  fmt::arg("line_no", current_experiment.lineno));
  console_logger->flush();
  jcoz_logger->info("experiment\tselected={class}:{line_no}\tspeedup={speedup}\tduration={duration}\nprogress-point\tname=end-to-end\ttype=source\tdelta={points_hit}",
               fmt::arg("speedup", current_experiment.speedup), fmt::arg("points_hit", current_experiment.points_hit),
               fmt::arg("duration", current_experiment.duration - current_experiment.delay), fmt::arg("class", sig),
               fmt::arg("line_no", current_experiment.lineno));
  jcoz_logger->flush();

  if (current_experiment.delay > current_experiment.duration || current_experiment.speedup == 0 && current_experiment.delay > 0)
  {
    console_logger->info("Last experiment: something went wrong with delays");
    console_logger->flush();
  }
  delete[] current_experiment.location_ranges;
  console_logger->info("Finished experiment, flushed logs, and delete current location ranges.");
}

void random_permutation(uint16_t* result, uint16_t size)
{
  for (uint16_t i = 0; i < size; ++i)
  {
    result[i] = i;
  }
  std::random_shuffle(result, result + size);
}

void JNICALL
Profiler::runAgentThread(jvmtiEnv *jvmti_env, JNIEnv *jni_env, void *args) {
  srand(time(nullptr));
  global_delay = 0;
  startup_time = std::chrono::high_resolution_clock::now().time_since_epoch();
  agent_pthread = pthread_self();

  if (print_traces)
  {
    traces_file = fopen("traces.txt", "w");
    printer = new StackTracesPrinter(traces_file, jvmti_env);
  }

  while (!__sync_bool_compare_and_swap(&user_threads_lock, 0, 1))
    ;
  std::atomic_thread_fence(std::memory_order_acquire);
  user_threads.erase(curr_ut);
  curr_ut = nullptr;
  user_threads_lock = 0;
  std::atomic_thread_fence(std::memory_order_release);
  //	usleep(warmup_time);
  prof_ready = true;

  while (_running) {
    console_logger->info("Starting next profiling loop. Collecting call frames for experiment...");
    console_logger->flush();

    if (print_traces)
    {
      while (!__sync_bool_compare_and_swap(&frame_lock, 0, 1));
      std::atomic_thread_fence(std::memory_order_acquire);
      frame_lock = 0;
      std::atomic_thread_fence(std::memory_order_release);
    }

    // 15 * SIGNAL_FREQ with randomization should give us roughly
    // the same number of iterations as doing 10 * SIGNAL_FREQ without
    // randomization.
    long total_needed_time = 15 * SIGNAL_FREQ;
    long total_accrued_time = 0;
    while (total_accrued_time < total_needed_time) {
      // Sleep some randomized time to avoid bias in the profiler.
      long curr_sleep = 2 * SIGNAL_FREQ - (rand() % SIGNAL_FREQ);
      jcoz_sleep(curr_sleep);
      signal_user_threads();
      total_accrued_time += curr_sleep;
      console_logger->debug("Slept for {sleep_time} time. {remaining_time} Remaining.",
                      fmt::arg("sleep_time", curr_sleep),
                      fmt::arg("remaining_time", total_needed_time - total_accrued_time));
    }

    while (!__sync_bool_compare_and_swap(&frame_lock, 0, 1))              // Acquire frame_lock
      ;
    std::atomic_thread_fence(std::memory_order_acquire);
    for (int i = 0; (i < call_index) && (i < NUM_CALL_FRAMES); i++) {
      call_frames.push_back(static_call_frames[i]);
    }
    frame_lock = 0;                                                       // Release frame_lock
    std::atomic_thread_fence(std::memory_order_release);
    uint16_t num_frames = call_frames.size();
    if (num_frames > 0) {
      console_logger->info("Had {} call frames. Checking for in scope call frame...", call_frames.size());
      call_index = 0;
      uint16_t permutation[num_frames];
      random_permutation(permutation, num_frames);
      JVMPI_CallFrame exp_frame;
      jint num_entries;
      jvmtiLineNumberEntry *entries = nullptr;
      for( int i = 0; i < num_frames; i++ ) {
        uint16_t j = permutation[i];
        console_logger->debug("Analysing frame #{}", j);
        console_logger->flush();
        exp_frame = call_frames.at(j);
        jvmtiError lineNumberError = jvmti->GetLineNumberTable(exp_frame.method_id, &num_entries, &entries);
        if( lineNumberError == JVMTI_ERROR_NONE ) {
          traces[j].is_selected = true;
          break;
        } else {
          jvmti->Deallocate((unsigned char *)entries);
        }
      }

      // If we don't find anything in scope, try again
      if( entries == nullptr ) {
        // TODO(dcv): Should we clear the call frames here?
        console_logger->debug("No in scope frames found. Trying again.");
        trace_idx = 0;
//        frame_lock = 0;
//        std::atomic_thread_fence(std::memory_order_release);
        continue;
      }

      if (print_traces)
      {
        fprintf(traces_file, "===================== Started next experiment. Select traces... =====================\n");
        for (uint16_t i = 0; i < trace_idx; ++i)
        {
          printer->PrintStackTrace(traces[i]);
        }
        trace_idx = 0;
      }

      console_logger->debug("Found in scope frames. Choosing a frame and running experiment...");
      current_experiment.method_id = exp_frame.method_id;
      current_experiment.bci = exp_frame.lineno;
      jint start_line;
      jint end_line; //exclusive
      jint line = -1;
      std::vector<std::pair<jint, jint>> location_ranges;

      bool select_last_line = true;
      for (int i = 1; i < num_entries; i++) {
        if (line == -1
            && entries[i].start_location > exp_frame.lineno) {
          line = entries[i - 1].line_number;
          current_experiment.lineno = line;
          select_last_line = false;
          break;
        }
      }

      if (select_last_line && num_entries > 0) {
          line = entries[num_entries - 1].line_number;
          current_experiment.lineno = line;
      }

      for (int i = 0; i < num_entries; i++) {
        if (entries[i].line_number == line) {
          if (i < num_entries - 1) {
            location_ranges.push_back(
                std::pair<jint, jint>(entries[i].start_location,
                  entries[i + 1].start_location));
          } else {
            location_ranges.push_back(
                std::pair<jint, jint>(entries[i].start_location,
                  MAX_BCI));
          }
        }
      }

      current_experiment.num_ranges = location_ranges.size();
      current_experiment.location_ranges =
        new std::pair<jint, jint>[location_ranges.size()];
      for (int i = 0; i < location_ranges.size(); i++) {
        current_experiment.location_ranges[i] = location_ranges[i];
      }
      call_index = 0;
//      frame_lock = 0;
//      std::atomic_thread_fence(std::memory_order_release);

      runExperiment(jni_env);
      while (!__sync_bool_compare_and_swap(&frame_lock, 0, 1))        // Acquire frame_lock
        ;
      std::atomic_thread_fence(std::memory_order_acquire);
      call_frames.clear();
      memset(static_call_frames, 0, NUM_CALL_FRAMES * sizeof(JVMPI_CallFrame));
      trace_idx = 0;
      frame_lock = 0;                                                 // Release frame_lock
      std::atomic_thread_fence(std::memory_order_release);
      jvmti->Deallocate((unsigned char *)entries);
      console_logger->info("Finished clearing frames and deallocating entries...");
    } else {
      console_logger->info("No frames found in agent thread. Trying sampling loop again...");
//      frame_lock = 0;
//      std::atomic_thread_fence(std::memory_order_release);
    }
  }

  if (print_traces)
  {
    delete printer;
    fclose(traces_file);
  }

  console_logger->info("Profiler done running...");
  profile_done = true;
}

bool Profiler::thread_in_main(jthread thread) {
  jvmtiThreadInfo info;
  jvmtiError err = jvmti->GetThreadInfo(thread, &info);
  if (err != JVMTI_ERROR_NONE) {
    if (err == JVMTI_ERROR_WRONG_PHASE) {
      return false;
    } else {
      exit(1);
    }
  }

  jvmtiThreadGroupInfo thread_grp;
  err = jvmti->GetThreadGroupInfo(info.thread_group, &thread_grp);
  if (err != JVMTI_ERROR_NONE) {
    if (err == JVMTI_ERROR_WRONG_PHASE) {
      return false;
    } else {
      exit(1);
    }
  }

  return !strcmp(thread_grp.name, "main");
}

void Profiler::addUserThread(jthread thread) {
  if (thread_in_main(thread)) {
    curr_ut = new struct UserThread();
    curr_ut->thread = pthread_self();
    curr_ut->local_delay = global_delay;
    curr_ut->java_thread = thread;
    curr_ut->points_hit = 0;
    // user threads lock
    while (!__sync_bool_compare_and_swap(&user_threads_lock, 0, 1))
      ;
    std::atomic_thread_fence(std::memory_order_acquire);
    user_threads.insert(curr_ut);
    user_threads_lock = 0;
    std::atomic_thread_fence(std::memory_order_release);
  } else {
    curr_ut = nullptr;
  }
}

void Profiler::removeUserThread(jthread thread) {
  if (curr_ut != nullptr) {
    console_logger->debug("Removing user thread");
    points_hit += curr_ut->points_hit;
    curr_ut->points_hit = 0;

    long sleep_time = global_delay - curr_ut->local_delay;
    if( sleep_time > 0 ) {
      jcoz_sleep(std::max(0L, sleep_time));
    } else {
      global_delay += std::labs(sleep_time);
    }

    // user threads lock
    while (!__sync_bool_compare_and_swap(&user_threads_lock, 0, 1))
      ;
    std::atomic_thread_fence(std::memory_order_acquire);
    user_threads.erase(curr_ut);
    user_threads_lock = 0;
    std::atomic_thread_fence(std::memory_order_release);

    delete curr_ut;
  }
}

bool inline Profiler::inExperiment(JVMPI_CallFrame &curr_frame) {
  if (curr_frame.method_id != current_experiment.method_id) {
    return false;
  }

  for (int i = 0; i < current_experiment.num_ranges; i++) {
    if (curr_frame.lineno >= current_experiment.location_ranges[i].first
        && curr_frame.lineno
        < current_experiment.location_ranges[i].second) {
      return true;
    }
  }
  return false;
}

bool inline Profiler::frameInScope(JVMPI_CallFrame &curr_frame) {
  return in_scope_ids.count((void *) curr_frame.method_id) > 0;
}

void Profiler::addInScopeMethods(jint method_count, jmethodID *methods) {
//  console_logger->info("Adding {:d} in scope methods", method_count);
  while (!__sync_bool_compare_and_swap(&in_scope_lock, 0, pthread_self()))
    ;
  std::atomic_thread_fence(std::memory_order_acquire);
  for (int i = 0; i < method_count; i++) {
    void *method = (void *)methods[i];
//    console_logger->info("Adding in scope method {}", method);
    in_scope_ids.insert(method);
  }
  in_scope_lock = 0;
  std::atomic_thread_fence(std::memory_order_release);
}

void Profiler::clearInScopeMethods(){
  console_logger->info("Clearing current in scope methods.");
  while (!__sync_bool_compare_and_swap(&in_scope_lock, 0, pthread_self()));
  in_scope_ids.clear();
  in_scope_lock = 0;
}

void Profiler::addProgressPoint(jint method_count, jmethodID *methods) {

  // Only ever set progress point once
  if( end_to_end || ((progress_point->method_id) != nullptr) ) {
    return;
  }

  for (int i = 0; i < method_count; i++) {
    jint entry_count;
    JvmtiScopedPtr<jvmtiLineNumberEntry> entries(jvmti);
    jvmtiError err = jvmti->GetLineNumberTable(methods[i], &entry_count, entries.GetRef());
    if( err != JVMTI_ERROR_NONE ) {
      console_logger->debug("Error getting line number entry table in addProgressPoint. Error: {}", err);

      continue;
    }

    for( int j = 0; j < entry_count; j++ ) {
      jvmtiLineNumberEntry curr_entry = entries.Get()[j];
      jint curr_lineno = curr_entry.line_number;
      if( curr_lineno == (progress_point->lineno) ) {
        progress_point->method_id = methods[i];
        progress_point->location = curr_entry.start_location;
        jvmti->SetBreakpoint(progress_point->method_id, progress_point->location);
        console_logger->info("Progress point set");
        return;
      }
    }
  }
  console_logger->error("Unable to set progress point");
}

void Profiler::canonicalize(std::string& scope) {
    std::replace(scope.begin(), scope.end(), '.', '/');
}

void Profiler::addScopeToIgnore(std::string& scope) {
    scopes_to_ignore.emplace_back(scope);
}

void Profiler::Handle(int signum, siginfo_t *info, void *context) {
  if( !prof_ready ) {
    return;
  }
  IMPLICITLY_USE(signum);
  IMPLICITLY_USE(info);

  JNIEnv *env = Accessors::CurrentJniEnv();
  if (env == nullptr) {

    return;
  }

  JVMPI_CallTrace trace;
  JVMPI_CallFrame frames[kMaxFramesToCapture];
  // We have to set every byte to 0 instead of just initializing the
  // individual fields, because the structs might be padded, and we
  // use memcmp on it later.  We can't use memset, because it isn't
  // async-safe.
  char *base = reinterpret_cast<char *>(frames);
  for (char *p = base;
      p < base + sizeof(JVMPI_CallFrame) * kMaxFramesToCapture; p++) {
    *p = 0;
  }

  trace.frames = frames;
  trace.env_id = env;

  ASGCTType asgct = Asgct::GetAsgct();
  (*asgct)(&trace, kMaxFramesToCapture, context);

  if (trace.num_frames < 0) {
    int idx = -trace.num_frames;
    if (idx > kNumCallTraceErrors) {
      return;
    }
  }

  if (!in_experiment) {

    // lock in scope
    curr_ut->local_delay = 0;
    bool has_lock = in_scope_lock == pthread_self();
    if (!has_lock) {
      while (!__sync_bool_compare_and_swap(&in_scope_lock, 0,
            pthread_self()))
        ;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    for (int i = 0; i < trace.num_frames; i++) {
      JVMPI_CallFrame &curr_frame = trace.frames[i];
      // if sampled stack trace contains one of prohibited methods, reject the whole trace
      if (is_prohibited(curr_frame.method_id)) break;

      if (frameInScope(curr_frame)) {
        // lock frame lock
        while (!__sync_bool_compare_and_swap(&frame_lock, 0, 1))
          ;
        std::atomic_thread_fence(std::memory_order_acquire);
        int index = call_index.fetch_add(1);
        if (index < NUM_CALL_FRAMES) {
          static_call_frames[index] = curr_frame;
        }
        if (print_traces)
        {
          JVMPI_CallFrame *fb = frame_buffer[trace_idx];
          for (int frame_num = 0; frame_num < trace.num_frames; ++frame_num)
          {
            base = reinterpret_cast<char *>(&(fb[frame_num]));
            // Make sure the padding is all set to 0.
            for (char *p = base; p < base + sizeof(JVMPI_CallFrame); p++) {
              *p = 0;
            }
            fb[frame_num].lineno = trace.frames[frame_num].lineno;
            fb[frame_num].method_id = trace.frames[frame_num].method_id;
          }
          traces[trace_idx].trace.frames = fb;
          traces[trace_idx].trace.num_frames = trace.num_frames;
          traces[trace_idx].selected_frame_idx = i;
          traces[trace_idx].is_selected = false;
          trace_idx = (trace_idx + 1) % kMaxStackTraces;
        }
        frame_lock = 0;
        std::atomic_thread_fence(std::memory_order_release);
        break;
      }
    }
    if (!has_lock) {
      in_scope_lock = 0;
    }
    std::atomic_thread_fence(std::memory_order_release);
  } else {

    curr_ut->num_signals_received++;
    for (int i = 0; i < trace.num_frames; i++) {
      JVMPI_CallFrame &curr_frame = trace.frames[i];
      if (inExperiment(curr_frame)) {
        curr_ut->local_delay += current_experiment.delay;
        break;
      }
    }

    if( curr_ut->num_signals_received == SAMPLES_BATCH_SIZE ) {
      long sleep_diff = global_delay - curr_ut->local_delay;
      if( sleep_diff > 0 ) {
        curr_ut->local_delay += jcoz_sleep(sleep_diff);
      } else {
        global_delay += std::labs(sleep_diff);
      }

      curr_ut->num_signals_received = 0;
    }

    points_hit += curr_ut->points_hit;
    curr_ut->points_hit = 0;
  }
}

struct sigaction SignalHandler::SetAction(
    void (*action)(int, siginfo_t *, void *)) {
  struct sigaction sa;
  sa.sa_handler = nullptr;
  sa.sa_sigaction = action;
  sa.sa_flags = SA_RESTART | SA_SIGINFO;

  sigemptyset(&sa.sa_mask);

  struct sigaction old_handler;
  if (sigaction(SIGPROF, &sa, &old_handler) != 0) {
    return old_handler;
  }

  return old_handler;
}

void Profiler::Start() {

  // old_action_ is stored, but never used.  This is in case of future
  // refactorings that need it.

  console_logger->info("Starting profiler...");
  jcoz_logger->set_pattern("%v");
  old_action_ = handler_.SetAction(&Profiler::Handle);
  std::srand(unsigned(std::time(0)));
  call_frames.reserve(2000);
  _running = true;
}

char *Profiler::getClassFromMethodIDLocation(jmethodID id) {
  jclass clazz;
  jvmtiError classErr = jvmti->GetMethodDeclaringClass(id, &clazz);
  if (classErr != JVMTI_ERROR_NONE) {
    return nullptr;
  }

  char *sig;
  jvmtiError classSigErr = jvmti->GetClassSignature(clazz, &sig, nullptr);
  if (classSigErr != JVMTI_ERROR_NONE) {
    return nullptr;
  }

  return sig;
}

void Profiler::cleanSignature(char *sig) {
  int sig_len = strlen(sig);
  if (sig_len < 3) {
    return;
  }

  for (int i = 0; i < sig_len - 1; i++) {
    sig[i] = sig[i + 1];
  }

  sig[sig_len - 2] = '\0';

  for (int i = 0; i < sig_len - 2; i++) {
    if (sig[i] == '/') {
      sig[i] = '.';
    } else if (sig[i] == '$') {
      sig[i] = '\0';
      return;
    }
  }
}

void Profiler::clearProgressPoint() {
  if( !end_to_end && (progress_point->method_id != nullptr) ) {
    console_logger->info("Clearing breakpoint");
    jvmti->ClearBreakpoint(progress_point->method_id, progress_point->location);
    progress_point->method_id = nullptr;
  }
}

void Profiler::Stop() {

  // Wait until we get to the end of the run
  // and then flush the profile output
  console_logger->info("Stopping profiler");
  if(_running){
    if (end_to_end) {
      points_hit++;
    }

    _running = false;

    console_logger->info("Waiting for profiler to finish current cycle...");
    while (!profile_done)
      ;

    console_logger->info("Profiler finished current cycle...");
  }

  std::vector<std::string> hits = bci_hits::create_dump();
  for (std::string& hit : hits) {
      console_logger->info("{}", hit);
  }
  clearInScopeMethods();
  signal(SIGPROF, SIG_IGN);
  console_logger->flush();
}

void Profiler::setJVMTI(jvmtiEnv *jvmti_env) {
  jvmti = jvmti_env;
}

void JNICALL
Profiler::HandleBreakpoint(
    jvmtiEnv *jvmti,
    JNIEnv *jni_env,
    jthread thread,
    jmethodID method_id,
    jlocation location
    ) {
  curr_ut->points_hit += in_experiment;
}

void Profiler::add_prohibited(jmethodID method_id)
{
    prohibited_methods.emplace(method_id);
}

bool Profiler::is_prohibited(jmethodID method_id)
{
    return prohibited_methods.find(method_id) != prohibited_methods.end();
}

void bci_hits::add_hit(char* class_fqn, jmethodID method_id, jint line_number, jint bci)
{
    _freqs[method_id][line_number][bci]++;
    _declaring_classes[method_id] = class_fqn;
}

std::vector<std::string> bci_hits::create_dump()
{
    std::vector<std::string> result;
    result.emplace_back("Bytecode index hits:");
    for (auto method_it = _freqs.begin(); method_it != _freqs.end(); ++method_it)
    {
        char* class_fqn = _declaring_classes[method_it->first];
        result.emplace_back(fmt::format("\tFor class {}:", class_fqn));
        for (auto line_it = method_it->second.begin(); line_it != method_it->second.end(); ++line_it)
        {
            jint line_number = line_it->first;
            std::stringstream ss;
            ss << "\t\t" << line_number << ": ";
            for (auto bci_it = line_it->second.begin(); bci_it != line_it->second.end(); ++bci_it)
            {
                ss << fmt::format("({}, {}); ", bci_it->first, bci_it->second);
            }
            result.emplace_back(ss.str());
        }
        free(class_fqn);
    }
    return result;
}


