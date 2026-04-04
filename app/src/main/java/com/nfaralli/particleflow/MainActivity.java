package com.yourpackage.name; // <-- change to your real package

import android.content.SharedPreferences;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.preference.PreferenceManager;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {
    private MediaPlayer mediaPlayer;
    private int currentTrack = 0; // 0: Silent

    private final SharedPreferences.OnSharedPreferenceChangeListener listener =
            (prefs, key) -> {
                if ("selected_track".equals(key)) {
                    updateAndPlayTrack();
                }
            };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // ... your usual onCreate code (e.g., setContentView)
        PreferenceManager.getDefaultSharedPreferences(this)
                .registerOnSharedPreferenceChangeListener(listener);

        updateAndPlayTrack(); // Play track on launch if not silent
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (currentTrack > 0 && mediaPlayer != null) {
            mediaPlayer.start();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        pauseMusic();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        releaseMusic();
        PreferenceManager.getDefaultSharedPreferences(this)
                .unregisterOnSharedPreferenceChangeListener(listener);
    }

    private void updateAndPlayTrack() {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        int selectedTrack;
        try {
            selectedTrack = Integer.parseInt(prefs.getString("selected_track", "0"));
        } catch (NumberFormatException e) {
            selectedTrack = 0;
        }
        if (selectedTrack != currentTrack) {
            releaseMusic();
            currentTrack = selectedTrack;
        }
        if (currentTrack > 0) {
            playMusic(currentTrack);
        } else {
            releaseMusic();
        }
    }

    private void playMusic(int trackNumber) {
        if (mediaPlayer != null) {
            // Already playing this track
            return;
        }
        int resId = getResources().getIdentifier(
                "music" + trackNumber, "raw", getPackageName());
        if (resId == 0) return; // No such resource

        mediaPlayer = MediaPlayer.create(this, resId);
        if (mediaPlayer != null) {
            mediaPlayer.setLooping(true);
            mediaPlayer.start();
        }
    }

    private void pauseMusic() {
        if (mediaPlayer != null && mediaPlayer.isPlaying()) {
            mediaPlayer.pause();
        }
    }

    private void releaseMusic() {
        if (mediaPlayer != null) {
            if (mediaPlayer.isPlaying()) {
                mediaPlayer.stop();
            }
            mediaPlayer.release();
            mediaPlayer = null;
        }
    }
}
