package com.nfaralli.particleflow;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class ParticlesSurfaceView extends SurfaceView
        implements SurfaceHolder.Callback {

    private static final String TAG = "ParticleFlow";
    private VulkanParticlesRenderer renderer;
    private RenderThread             renderThread;
    private volatile boolean         running = false;

    public ParticlesSurfaceView(Context context) {
        super(context);
        init(context);
    }

    public ParticlesSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    private void init(Context context) {
        renderer = new VulkanParticlesRenderer(context);
        getHolder().addCallback(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        CrashReport.trace("surfaceCreated");
        if (renderThread != null && renderThread.isAlive()) {
            Log.e(TAG, "Previous render thread is still running; not starting another one");
            return;
        }
        running = true;
        renderThread = new RenderThread(holder);
        renderThread.start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        CrashReport.trace("surfaceChanged " + w + "x" + h);
        renderer.onSurfaceChanged(w, h);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        CrashReport.trace("surfaceDestroyed");
        running = false;
        boolean stopped = true;
        try {
            if (renderThread != null) {
                renderThread.join(3000);
                stopped = !renderThread.isAlive();
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            stopped = false;
        }
        if (stopped) {
            renderer.destroy();   // a new engine is created in initialize() for the next surface
        } else {
            // Destroying the engine under a running render thread would be a use-after-free.
            Log.e(TAG, "Render thread did not stop in time; leaving the engine alive");
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action     = event.getActionMasked();
        int pointerIdx = event.getActionIndex();
        int pointerId  = event.getPointerId(pointerIdx);

        // Original behaviour: every DOWN/MOVE event refreshes the points of the fingers that are
        // present and counts a "miss" for the others (3 misses -> that point is disabled).
        // UP/CANCEL are ignored on purpose: the last finger's point stays where it was.
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_MOVE: {
                int present = 0;
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int id = event.getPointerId(i);
                    if (id < VulkanParticlesRenderer.MAX_TOUCH_POINTS) {
                        present |= (1 << id);
                        renderer.setTouchPoint(id, event.getX(i), event.getY(i));
                    }
                }
                renderer.touchesMissed(present);
                break;
            }
            default:
                break;
        }
        return true;
    }

    public VulkanParticlesRenderer getRenderer() { return renderer; }
    public void applySettings() { renderer.applyParameters(); renderer.resetParticles(); }
    public void initTesla369()  { renderer.initTesla369(); }

    private class RenderThread extends Thread {
        private final SurfaceHolder holder;
        private static final long TARGET_FRAME_MS = 16;

        RenderThread(SurfaceHolder holder) {
            this.holder = holder;
            setName("ParticleFlow-Render");
        }

        @Override
        public void run() {
            try {
                if (!renderer.initialize(holder.getSurface())) {
                    Log.e(TAG, "Failed to initialize Vulkan renderer");
                    return;
                }
                while (running) {
                    long start = System.currentTimeMillis();
                    renderer.drawFrame();
                    long sleep = TARGET_FRAME_MS - (System.currentTimeMillis() - start);
                    if (sleep > 0) {
                        try { Thread.sleep(sleep); }
                        catch (InterruptedException e) { Thread.currentThread().interrupt(); }
                    }
                }
            } catch (Throwable t) {
                Log.e(TAG, "Render thread crashed", t);
            }
        }
    }
}
