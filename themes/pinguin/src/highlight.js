'use strict';
import hljs from 'highlight.js';

/**
 * Registers `clike` as an alias for highlight.js's built-in `c` language
 * definition, so code blocks marked with `language-clike` are highlighted
 * correctly.
 *
 * @see {@link https://highlightjs.readthedocs.io/en/latest/api.html#registeraliases}
 */
hljs.registerAliases(['clike'], { languageName: 'c' });

/**
 * Applies highlight.js syntax highlighting to all `<pre><code>` blocks in
 * the document that have not already been highlighted.
 *
 * @see {@link https://highlightjs.readthedocs.io/en/latest/api.html#highlightall}
 */
hljs.highlightAll();

/**
 * Synchronises the active highlight.js stylesheet with the site theme.
 *
 * Expects two `<link>` elements in the document:
 * - `#hljs-light` — the stylesheet to enable in light mode.
 * - `#hljs-dark`  — the stylesheet to enable in dark mode.
 *
 * If either element is absent the function exits early without error.
 * After an initial sync, a `MutationObserver` watches the `class` attribute
 * on `<html>` and re-applies the correct stylesheet whenever the `dark`
 * class is toggled.
 *
 * @listens MutationObserver~observe — `document.documentElement` class changes
 */
(function syncHljs() {

    /**
     * The highlight.js light-mode stylesheet `<link>` element.
     * @type {HTMLLinkElement | null}
     */
    const light = document.getElementById('hljs-light');

    /**
     * The highlight.js dark-mode stylesheet `<link>` element.
     * @type {HTMLLinkElement | null}
     */
    const dark = document.getElementById('hljs-dark');

    if (!light || !dark) return;

    /**
     * Reads the current theme from `<html>` and enables the matching
     * highlight.js stylesheet while disabling the other.
     *
     * @inner
     * @returns {void}
     */
    function apply() {
        const isDark = document.documentElement.classList.contains('dark');
        light.disabled = isDark;
        dark.disabled  = !isDark;
    }

    // Synchronise immediately to match the theme present at script execution.
    apply();

    /**
     * Observes class attribute mutations on `<html>` so `apply` is called
     * whenever the `dark` class is added or removed by the theme toggle.
     *
     * @type {MutationObserver}
     */
    new MutationObserver(apply)
        .observe(document.documentElement, { attributes: true, attributeFilter: ['class'] });
})();