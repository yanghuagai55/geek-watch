#include "media_monitor.h"

#include <chrono>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

namespace {

using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionPlaybackStatus;

std::string StatusText(
    GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
  switch (status) {
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
      return "playing";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
      return "paused";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
      return "stopped";
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
      return "closed";
    default:
      return "unknown";
  }
}

}  // namespace

MediaMonitor::~MediaMonitor() { Stop(); }

void MediaMonitor::Start() {
  if (worker_.joinable()) return;
  stop_ = false;
  worker_ = std::thread(&MediaMonitor::Loop, this);
}

void MediaMonitor::Stop() {
  stop_ = true;
  command_cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

MediaMonitor::Info MediaMonitor::Get() {
  std::lock_guard<std::mutex> lock(mutex_);
  return info_;
}

void MediaMonitor::Control(const std::string& action) {
  {
    std::lock_guard<std::mutex> lock(command_mutex_);
    commands_.push_back(action);
  }
  command_cv_.notify_one();
}

void MediaMonitor::Loop() {
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (...) {
    return;
  }

  GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
  while (!stop_) {
    Info next;
    try {
      if (!manager) {
        manager = GlobalSystemMediaTransportControlsSessionManager::
                      RequestAsync()
                          .get();
      }
      auto session = manager ? manager.GetCurrentSession() : nullptr;

      std::vector<std::string> commands;
      {
        std::lock_guard<std::mutex> lock(command_mutex_);
        commands.swap(commands_);
      }
      if (session && !commands.empty()) {
        for (const auto& action : commands) {
          try {
            if (action == "play") {
              session.TryPlayAsync().get();
            } else if (action == "pause") {
              session.TryPauseAsync().get();
            } else if (action == "toggle") {
              session.TryTogglePlayPauseAsync().get();
            } else if (action == "next") {
              session.TrySkipNextAsync().get();
            } else if (action == "previous") {
              session.TrySkipPreviousAsync().get();
            }
          } catch (...) {
          }
        }
      }

      if (session) {
        next.has_session = true;
        try {
          auto properties = session.TryGetMediaPropertiesAsync().get();
          next.title = winrt::to_string(properties.Title());
          next.artist = winrt::to_string(properties.Artist());
        } catch (...) {
        }
        try {
          next.status = StatusText(session.GetPlaybackInfo().PlaybackStatus());
        } catch (...) {
        }
        try {
          next.app_id = winrt::to_string(session.SourceAppUserModelId());
        } catch (...) {
        }
        try {
          auto timeline = session.GetTimelineProperties();
          next.position_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 timeline.Position())
                                 .count();
          next.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 timeline.EndTime() - timeline.StartTime())
                                 .count();
        } catch (...) {
        }
      }
    } catch (...) {
      manager = nullptr;
      next = Info{};
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      info_ = std::move(next);
    }

    std::unique_lock<std::mutex> lock(command_mutex_);
    command_cv_.wait_for(lock, std::chrono::milliseconds(800), [this]() {
      return stop_ || !commands_.empty();
    });
  }
}
