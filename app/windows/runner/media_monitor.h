#ifndef RUNNER_MEDIA_MONITOR_H_
#define RUNNER_MEDIA_MONITOR_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 读取 Windows 全局媒体会话（SMTC）的当前标题、艺人与播放状态。
// WinRT 调用放在独立 MTA 线程里，平台线程只读缓存，避免阻塞 UI。
class MediaMonitor {
 public:
  struct Info {
    bool has_session = false;
    std::string title;
    std::string artist;
    std::string app_id;
    std::string status;  // playing / paused / stopped / closed / unknown
    int64_t position_ms = 0;
    int64_t duration_ms = 0;
  };

  MediaMonitor() = default;
  ~MediaMonitor();

  MediaMonitor(const MediaMonitor&) = delete;
  MediaMonitor& operator=(const MediaMonitor&) = delete;

  void Start();
  void Stop();
  Info Get();

  /// 投递播放控制：play / pause / toggle / next / previous。
  void Control(const std::string& action);

 private:
  void Loop();

  std::atomic<bool> stop_{false};
  std::thread worker_;
  std::mutex mutex_;
  Info info_;

  std::mutex command_mutex_;
  std::condition_variable command_cv_;
  std::vector<std::string> commands_;
};

#endif  // RUNNER_MEDIA_MONITOR_H_
