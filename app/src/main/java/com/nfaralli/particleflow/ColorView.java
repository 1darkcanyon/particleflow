package com.nfaralli.particleflow;

import android.content.Context;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.widget.LinearLayout;

/**
 * Compound view: three ValidatedEditText fields for R, G, B (0–255).
 * Inflates color_view.xml. Fires onColorChanged whenever any channel changes.
 */
public class ColorView extends LinearLayout {

    public interface OnColorChangedListener {
        void onColorChanged(int r, int g, int b);
    }

    private ValidatedEditText etR, etG, etB;
    private OnColorChangedListener listener;
    private boolean suppressing = false;

    public ColorView(Context context) {
        super(context);
        init(context);
    }

    public ColorView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init(context);
    }

    public ColorView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        init(context);
    }

    private void init(Context context) {
        LayoutInflater.from(context).inflate(R.layout.color_view, this, true);
        etR = findViewById(R.id.bgColorR);
        etG = findViewById(R.id.bgColorG);
        etB = findViewById(R.id.bgColorB);

        etR.setRange(0, 255);
        etG.setRange(0, 255);
        etB.setRange(0, 255);

        android.text.TextWatcher watcher = new android.text.TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int i, int c, int a) {}
            @Override public void onTextChanged(CharSequence s, int i, int b, int c) {}
            @Override
            public void afterTextChanged(android.text.Editable s) {
                if (!suppressing && listener != null) {
                    listener.onColorChanged(etR.getValue(), etG.getValue(), etB.getValue());
                }
            }
        };
        etR.addTextChangedListener(watcher);
        etG.addTextChangedListener(watcher);
        etB.addTextChangedListener(watcher);
    }

    public void setOnColorChangedListener(OnColorChangedListener l) {
        this.listener = l;
    }

    public int getR() { return etR.getValue(); }
    public int getG() { return etG.getValue(); }
    public int getB() { return etB.getValue(); }

    public void setColor(int r, int g, int b) {
        suppressing = true;
        etR.setValue(r);
        etG.setValue(g);
        etB.setValue(b);
        suppressing = false;
    }
}
