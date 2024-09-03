---
title: Caps Word Behavior
sidebar_label: Caps Word
---

## Summary

The caps word behavior behaves similar to a caps lock, but will automatically deactivate when any key not in a continue list is pressed, or if the caps word key is pressed again. For smaller keyboards using [mod-taps](mod-tap.md), this can help avoid repeated alternating holds when typing words in all caps.

The modifiers are applied only to the alphabetic (`A` to `Z`) keycodes, to avoid automatically applying them to numeric values, etc.

### Behavior Binding

- Reference: `&caps_word`

Example:

```dts
&caps_word
```

### Configuration

#### Continue list

By default, the caps word will remain active when any alphanumeric character or underscore (`UNDERSCORE`), backspace (`BACKSPACE`), or delete (`DELETE`) characters are pressed. Any other non-modifier keycode sent will turn off caps word. If you would like to override this, you can set a new array of keys in the `continue-list` property in your keymap:

```dts
&caps_word {
    shift-list = <DOT>;
};

/ {
    keymap {
        ...
    };
};
```

#### Applied modifier(s)

In addition, if you would like _multiple_ modifiers, instead of just `MOD_LSFT`, you can override the `mods` property:

```dts
&caps_word {
    mods = <(MOD_LSFT | MOD_LALT)>;
};
```

### Multiple Caps Breaks

You can add multiple caps words instances with different sets of properties in your keymap:

```dts
/ {
    behaviors {
        caps_sentence: caps_sentence {
            compatible = "zmk,behavior-caps-word";
            #binding-cells = <0>;
            continue-list = <SPACE>;
        };

        ctrl_word: ctrl_word {
            compatible = "zmk,behavior-caps-word";
            #binding-cells = <0>;
            mods = <MOD_LCTL>;
        };
    };

    keymap {
        ...
    };
};
```
