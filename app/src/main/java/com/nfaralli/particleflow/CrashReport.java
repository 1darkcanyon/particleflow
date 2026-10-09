package com.nfaralli.particleflow;

import android.annotation.TargetApi;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Tiny on-device crash reporter (no adb needed).
 *  - Java and native code append breadcrumbs ("trace") to files/pf_trace.txt.
 *  - On the next launch, consumeReport() combines Android's record of why the previous process
 *    died (ApplicationExitInfo, incl. the native tombstone) with the last breadcrumbs.
 */
public final class CrashReport {

    private static final String TRACE_NAME = "pf_trace.txt";
    private static final String PREFS      = "pf_crashreport";
    private static final Object LOCK       = new Object();
    private static volatile File traceFile;

    private CrashReport() {}

    public static void init(Context ctx) {
        traceFile = new File(ctx.getFilesDir(), TRACE_NAME);

        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try {
                StringWriter sw = new StringWriter();
                e.printStackTrace(new PrintWriter(sw));
                trace("JAVA CRASH in thread " + t.getName() + ":\n" + sw);
            } catch (Throwable ignored) { }
            if (prev != null) prev.uncaughtException(t, e);
        });
    }

    public static String tracePath() {
        File f = traceFile;
        return f == null ? null : f.getAbsolutePath();
    }

    /** Appends one breadcrumb line. Survives a process crash (plain append, no buffering). */
    public static void trace(String msg) {
        File f = traceFile;
        if (f == null) return;
        synchronized (LOCK) {
            try (FileOutputStream out = new FileOutputStream(f, true)) {
                String line = new SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(new Date())
                        + " J: " + msg + "\n";
                out.write(line.getBytes("UTF-8"));
            } catch (IOException ignored) { }
        }
    }

    /**
     * Returns a report if the previous run ended badly (crash, ANR, killed...), else null.
     * Always clears the trace file so the new run starts with a clean one.
     */
    public static String consumeReport(Context ctx) {
        StringBuilder sb = new StringBuilder();
        boolean bad = false;

        SharedPreferences sp = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long lastShown = sp.getLong("lastShown", 0);
        long newest = lastShown;

        sb.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
          .append("  Android ").append(Build.VERSION.RELEASE)
          .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n\n");

        if (Build.VERSION.SDK_INT >= 30) {
            try {
                BadExit r = readExitInfo(ctx, lastShown);
                if (r != null) {
                    bad = r.bad;
                    newest = Math.max(newest, r.newest);
                    sb.append(r.text);
                }
            } catch (Throwable t) {
                sb.append("(could not read exit info: ").append(t).append(")\n");
            }
        }

        String tail = readTraceTail(60);
        if (Build.VERSION.SDK_INT < 30 && tail.contains("JAVA CRASH")) bad = true;

        if (!tail.isEmpty()) {
            sb.append("\n--- breadcrumbs from the last run (J = Java, N = native) ---\n").append(tail);
        }

        File f = traceFile;
        if (f != null) f.delete();
        if (newest > lastShown) sp.edit().putLong("lastShown", newest).apply();

        return bad ? sb.toString() : null;
    }

    private static final class BadExit {
        boolean bad; long newest; String text;
    }

    @TargetApi(30)
    private static BadExit readExitInfo(Context ctx, long lastShown) {
        ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(ctx.getPackageName(), 0, 3);
        BadExit out = new BadExit();
        StringBuilder sb = new StringBuilder();
        for (ApplicationExitInfo i : list) {
            if (i.getTimestamp() <= lastShown) continue;
            out.newest = Math.max(out.newest, i.getTimestamp());
            int reason = i.getReason();
            boolean isBad = reason == ApplicationExitInfo.REASON_CRASH
                    || reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                    || reason == ApplicationExitInfo.REASON_ANR
                    || reason == ApplicationExitInfo.REASON_SIGNALED
                    || reason == ApplicationExitInfo.REASON_LOW_MEMORY
                    || reason == ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE
                    || reason == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE;
            out.bad |= isBad;
            sb.append(new SimpleDateFormat("MM-dd HH:mm:ss", Locale.US).format(new Date(i.getTimestamp())))
              .append("  exit reason=").append(reasonName(reason))
              .append(" status=").append(i.getStatus())
              .append(" importance=").append(i.getImportance())
              .append("\n  description: ").append(i.getDescription()).append('\n');
            if (isBad && (reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                    || reason == ApplicationExitInfo.REASON_ANR)) {
                try (InputStream in = i.getTraceInputStream()) {
                    if (in != null) {
                        sb.append("  --- native crash / ANR details (readable strings) ---\n");
                        appendPrintable(in, sb, 150);
                    }
                } catch (IOException e) {
                    sb.append("  (could not read trace: ").append(e).append(")\n");
                }
            }
            sb.append('\n');
        }
        out.text = sb.toString();
        return out;
    }

    private static String reasonName(int r) {
        switch (r) {
            case 1:  return "EXIT_SELF";
            case 2:  return "SIGNALED";
            case 3:  return "LOW_MEMORY";
            case 4:  return "CRASH (java)";
            case 5:  return "CRASH_NATIVE";
            case 6:  return "ANR";
            case 7:  return "INITIALIZATION_FAILURE";
            case 8:  return "PERMISSION_CHANGE";
            case 9:  return "EXCESSIVE_RESOURCE_USAGE";
            case 10: return "USER_REQUESTED";
            case 11: return "USER_STOPPED";
            case 12: return "DEPENDENCY_DIED";
            case 13: return "OTHER";
            default: return "UNKNOWN(" + r + ")";
        }
    }

    /** Pulls readable ASCII runs out of a (possibly binary protobuf) tombstone. */
    private static void appendPrintable(InputStream in, StringBuilder sb, int maxLines) throws IOException {
        byte[] buf = new byte[256 * 1024];
        int n = 0, r;
        while (n < buf.length && (r = in.read(buf, n, buf.length - n)) > 0) n += r;
        StringBuilder run = new StringBuilder();
        int lines = 0;
        for (int k = 0; k < n && lines < maxLines; k++) {
            int c = buf[k] & 0xff;
            if (c >= 0x20 && c < 0x7f) {
                run.append((char) c);
            } else {
                if (run.length() >= 6) {
                    sb.append("  ").append(run).append('\n');
                    lines++;
                }
                run.setLength(0);
            }
        }
    }

    private static String readTraceTail(int maxLines) {
        File f = traceFile;
        if (f == null || !f.exists()) return "";
        List<String> lines = new ArrayList<>();
        try (BufferedReader br = new BufferedReader(
                new InputStreamReader(new FileInputStream(f), "UTF-8"))) {
            String l;
            while ((l = br.readLine()) != null) lines.add(l);
        } catch (IOException e) {
            return "(could not read trace: " + e + ")\n";
        }
        int from = Math.max(0, lines.size() - maxLines);
        StringBuilder sb = new StringBuilder();
        for (int i = from; i < lines.size(); i++) sb.append(lines.get(i)).append('\n');
        return sb.toString();
    }
}
