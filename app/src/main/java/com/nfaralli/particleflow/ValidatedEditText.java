package com.nfaralli.particleflow;

import android.content.Context;
import android.graphics.Color;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.AttributeSet;
import androidx.appcompat.widget.AppCompatEditText;

/**
 * EditText that validates integer input and highlights out-of-range values.
 * Call setRange(min, max) after inflation to configure bounds.
 */
public class ValidatedEditText extends AppCompatEditText {

    private int min = Integer.MIN_VALUE;
    private int max = Integer.MAX_VALUE;
    private boolean valid = true;

    public ValidatedEditText(Context context) {
        super(context);
        init();
    }

    public ValidatedEditText(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public ValidatedEditText(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        init();
    }

    private void init() {
        addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override
            public void afterTextChanged(Editable s) {
                validate();
            }
        });
    }

    public void setRange(int min, int max) {
        this.min = min;
        this.max = max;
        validate();
    }

    public boolean isValid() {
        return valid;
    }

    /** Returns the clamped integer value, or the min if input is invalid. */
    public int getValue() {
        try {
            int v = Integer.parseInt(getText().toString());
            return Math.min(max, Math.max(min, v));
        } catch (NumberFormatException e) {
            return min;
        }
    }

    public void setValue(int value) {
        setText(String.valueOf(value));
    }

    private void validate() {
        try {
            int v = Integer.parseInt(getText().toString());
            valid = (v >= min && v <= max);
        } catch (NumberFormatException e) {
            valid = false;
        }
        setTextColor(valid ? Color.BLACK : Color.RED);
    }
}
