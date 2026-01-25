#ifndef MEDIA_MANAGER_HPP
#define MEDIA_MANAGER_HPP

#include <string>
#include <vector>

enum class AudioRoute {
    SPEAKER,
    HEADPHONE,
    BLUETOOTH,
    EARPIECE
};

enum class MediaType {
    AUDIO,
    VIDEO,
    IMAGE
};

// ============================================================================
// Media Manager Class
// ============================================================================
class MediaManager {
public:
    static MediaManager& getInstance();
    
    // Audio Management
    bool playAudio(const std::string& file_path);
    bool pauseAudio();
    bool stopAudio();
    bool setVolume(int volume_level);
    int getVolume();
    bool muteAudio();
    bool unmuteAudio();
    bool setAudioRoute(AudioRoute route);
    std::string getAudioStatus();
    
    // Video Playback
    bool playVideo(const std::string& file_path);
    bool pauseVideo();
    bool stopVideo();
    bool setVideoQuality(const std::string& quality);
    bool setVideoSubtitles(bool enable);
    
    // Notification & Sound Management
    bool playNotificationSound(const std::string& sound_id);
    bool stopNotificationSound();
    bool setNotificationVolume(int volume);
    bool enableVibration();
    bool disableVibration();
    bool vibratePattern(const std::string& pattern);
    
    // Ringtone Management
    bool setRingtone(const std::string& ringtone_path);
    std::string getCurrentRingtone();
    bool setNotificationTone(const std::string& tone_path);
    
    // Haptics & Feedback
    bool enableHaptics();
    bool disableHaptics();
    bool playHapticFeedback(int duration_ms);
    
    // System Sounds
    bool playSoundEffect(const std::string& effect_name);
    
private:
    MediaManager();
    ~MediaManager();
    
    int current_volume;
    bool is_muted;
    AudioRoute current_audio_route;
    std::string current_playback_file;
};

#endif // MEDIA_MANAGER_HPP
