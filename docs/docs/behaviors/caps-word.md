---
title: Caps Word Behavior
sidebar_label: Caps Word
---

## Summary

The caps word behavior behaves similar to a caps lock, but will automatically deactivate when any key not in a continue list is pressed, or if the caps word key is pressed again. For smaller keyboards using [mod-taps](/docs/behaviors/mod-tap), this can help avoid repeated alternating holds when typing words in all caps.

## Caps Word

Applies shift to capitalize alphabetic keys. Remains active but does not apply shift when numeric keys, underscore, backspace, or delete are pressed.

### Behavior Binding

- Reference: `&caps_word`

Example:

```dts
&caps_word
```

## Programmer Word

This is identical to `&caps_word` except that shift is also applied to `MINUS`. This can be useful when programming to type certain symbols. For example, enabling `&prog_word` and typing `some-constant` results in `SOME_CONSTANT`.

### Behavior Binding

- Reference: `&prog_word`

Example:

```dts
&prog_word
```

## Configuration

### Shift List

Caps word will apply shift to the following:

- Alphabetic keys (`A` through `Z`).
- Keys in the `shift-list` property (defaults to empty for `&caps_word` and `MINUS` for `&prog_word`).

You can add more key codes to be shifted by overriding the `shift-list` property in your keymap:
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
