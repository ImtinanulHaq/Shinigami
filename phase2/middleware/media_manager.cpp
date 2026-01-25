#include "media_manager.hpp"
#include <iostream>
#include <sstream>

MediaManager::MediaManager() {
    current_volume = 50;
    is_muted = false;
    current_audio_route = AudioRoute::SPEAKER;
    current_playback_file = "";
}

MediaManager::~MediaManager() {
}

MediaManager& MediaManager::getInstance() {
    static MediaManager instance;
    return instance;
}

bool MediaManager::playAudio(const std::string& file_path) {
    current_playback_file = file_path;
    std::cout << "INFO: Playing audio: " << file_path << "\n";
    return true;
}

bool MediaManager::pauseAudio() {
    std::cout << "INFO: Audio paused\n";
    return true;
}

bool MediaManager::stopAudio() {
    current_playback_file = "";
    std::cout << "INFO: Audio stopped\n";
    return true;
}

bool MediaManager::setVolume(int volume_level) {
    if (volume_level < 0 || volume_level > 100) return false;
    current_volume = volume_level;
    std::cout << "INFO: Volume set to " << volume_level << "%\n";
    return true;
}

int MediaManager::getVolume() {
    return current_volume;
}

bool MediaManager::muteAudio() {
    is_muted = true;
    std::cout << "INFO: Audio muted\n";
    return true;
}

bool MediaManager::unmuteAudio() {
    is_muted = false;
    std::cout << "INFO: Audio unmuted\n";
    return true;
}

bool MediaManager::setAudioRoute(AudioRoute route) {
    current_audio_route = route;
    std::string route_name;
    switch (route) {
        case AudioRoute::SPEAKER: route_name = "SPEAKER"; break;
        case AudioRoute::HEADPHONE: route_name = "HEADPHONE"; break;
        case AudioRoute::BLUETOOTH: route_name = "BLUETOOTH"; break;
        case AudioRoute::EARPIECE: route_name = "EARPIECE"; break;
    }
    std::cout << "INFO: Audio routed to " << route_name << "\n";
    return true;
}

std::string MediaManager::getAudioStatus() {
    std::ostringstream oss;
    oss << "Audio Status:\n";
    oss << "  Playing: " << (!current_playback_file.empty() ? "Yes" : "No") << "\n";
    oss << "  File: " << (current_playback_file.empty() ? "None" : current_playback_file) << "\n";
    oss << "  Volume: " << current_volume << "%\n";
    oss << "  Muted: " << (is_muted ? "Yes" : "No") << "\n";
    return oss.str();
}

bool MediaManager::playVideo(const std::string& file_path) {
    std::cout << "INFO: Playing video: " << file_path << "\n";
    return true;
}

bool MediaManager::pauseVideo() {
    std::cout << "INFO: Video paused\n";
    return true;
}

bool MediaManager::stopVideo() {
    std::cout << "INFO: Video stopped\n";
    return true;
}

bool MediaManager::setVideoQuality(const std::string& quality) {
    std::cout << "INFO: Video quality set to " << quality << "\n";
    return true;
}

bool MediaManager::setVideoSubtitles(bool enable) {
    std::cout << "INFO: Video subtitles " << (enable ? "enabled" : "disabled") << "\n";
    return true;
}

bool MediaManager::playNotificationSound(const std::string& sound_id) {
    std::cout << "INFO: Playing notification sound: " << sound_id << "\n";
    return true;
}

bool MediaManager::stopNotificationSound() {
    std::cout << "INFO: Notification sound stopped\n";
    return true;
}

bool MediaManager::setNotificationVolume(int volume) {
    if (volume < 0 || volume > 100) return false;
    std::cout << "INFO: Notification volume set to " << volume << "%\n";
    return true;
}

bool MediaManager::enableVibration() {
    std::cout << "INFO: Vibration enabled\n";
    return true;
}

bool MediaManager::disableVibration() {
    std::cout << "INFO: Vibration disabled\n";
    return true;
}

bool MediaManager::vibratePattern(const std::string& pattern) {
    std::cout << "INFO: Vibration pattern: " << pattern << "\n";
    return true;
}

bool MediaManager::setRingtone(const std::string& ringtone_path) {
    std::cout << "INFO: Ringtone set to: " << ringtone_path << "\n";
    return true;
}

std::string MediaManager::getCurrentRingtone() {
    return "/system/media/ringtone_default.ogg";
}

bool MediaManager::setNotificationTone(const std::string& tone_path) {
    std::cout << "INFO: Notification tone set to: " << tone_path << "\n";
    return true;
}

bool MediaManager::enableHaptics() {
    std::cout << "INFO: Haptic feedback enabled\n";
    return true;
}

bool MediaManager::disableHaptics() {
    std::cout << "INFO: Haptic feedback disabled\n";
    return true;
}

bool MediaManager::playHapticFeedback(int duration_ms) {
    std::cout << "INFO: Playing haptic feedback for " << duration_ms << "ms\n";
    return true;
}

bool MediaManager::playSoundEffect(const std::string& effect_name) {
    std::cout << "INFO: Playing sound effect: " << effect_name << "\n";
    return true;
}
