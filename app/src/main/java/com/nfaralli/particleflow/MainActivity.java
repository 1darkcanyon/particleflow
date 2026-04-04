package com.nfaralli.particleflow;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;

public class MainActivity extends Activity {

    private ParticlesSurfaceView surfaceView;
    private MediaPlayer mediaPlayer;
    private boolean musicWasPlaying = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        try {
            surfaceView = new ParticlesSurfaceView(this);

            // Root frame that layers the surface view with the gear button overlay
            FrameLayout frame = new FrameLayout(this);
            frame.addView(surfaceView);

            // Floating gear button in the top-right corner
            Button gearBtn = new Button(this);
            gearBtn.setText("⚙");
            gearBtn.setTextSize(20);
            gearBtn.setAlpha(0.6f);
            gearBtn.setBackgroundColor(android.graphics.Color.TRANSPARENT);
            gearBtn.setContentDescription(getString(R.string.settings_button_desc));
            int margin = dp(8);
            FrameLayout.LayoutParams gearParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.END
            );
            gearParams.topMargin   = margin;
            gearParams.setMarginEnd(margin);
            gearBtn.setLayoutParams(gearParams);
            gearBtn.setOnClickListener(v -> showSettings());
            frame.addView(gearBtn);

            setContentView(frame);
        } catch (Throwable t) {
            // Show error on screen so we can read it
            new AlertDialog.Builder(this)
                .setTitle("NEXUS Crash")
                .setMessage(t.getClass().getSimpleName() + "\n\n" + t.getMessage())
                .setPositiveButton("OK", null)
                .show();
        }

        loadAndStartMusic();
    }

    private void loadAndStartMusic() {
        SharedPreferences prefs = getSharedPreferences(
            VulkanParticlesRenderer.PREFS_NAME, MODE_PRIVATE);
        int track = prefs.getInt("selected_track", 0);
        applyMusicTrack(track);
    }

    public void applyMusicTrack(int trackIndex) {
        if (mediaPlayer != null) {
            mediaPlayer.release();
            mediaPlayer = null;
        }
        if (trackIndex < 1 || trackIndex > 7) return;
        int resId = getResources().getIdentifier(
            "music" + trackIndex, "raw", getPackageName());
        if (resId == 0) return;
        mediaPlayer = MediaPlayer.create(this, resId);
        if (mediaPlayer != null) {
            mediaPlayer.setLooping(true);
            mediaPlayer.start();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mediaPlayer != null && mediaPlayer.isPlaying()) {
            mediaPlayer.pause();
            musicWasPlaying = true;
        } else {
            musicWasPlaying = false;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mediaPlayer != null && musicWasPlaying && !mediaPlayer.isPlaying()) {
            mediaPlayer.start();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mediaPlayer != null) {
            mediaPlayer.release();
            mediaPlayer = null;
        }
    }

    public void initTesla369() {
        if (surfaceView != null) surfaceView.initTesla369();
    }

    public void showSettings() {
        if (surfaceView != null) {
            SettingsView settings = new SettingsView(this, surfaceView.getRenderer());
            settings.show();
        }
    }

    private int dp(int dp) {
        float density = getResources().getDisplayMetrics().density;
        return (int)(dp * density + 0.5f);
    }
}
