'use strict';
import hljs from 'highlight.js';

/**
 * Applies highlight.js syntax highlighting to all `<pre><code>` blocks in
 * the document that have not already been highlighted.
 *
 * @see {@link https://highlightjs.readthedocs.io/en/latest/api.html#highlightall}
 */
hljs.highlightAll();

/**
 * Synchronizes the active highlight.js stylesheet with the site theme.
 *
 * Expects two `<link>` elements in the document:
 * - `#hljs-theme-light` — the stylesheet to enable in light mode.
 * - `#hljs-theme-dark`  — the stylesheet to enable in dark mode.
 *
 * If either element is absent the function exits early without error.
 * After an initial sync, a `MutationObserver` watches the `data-theme`
 * attribute on `<html>` and re-applies the correct stylesheet whenever
 * the theme is toggled.
 *
 * @listens MutationObserver~observe — `document.documentElement` data-theme changes
 */
(function syncHljs() {

    /**
     * The highlight.js light-mode stylesheet `<link>` element.
     * @type {HTMLLinkElement | null}
     */
    const light = document.getElementById('hljs-theme-light');

    /**
     * The highlight.js dark-mode stylesheet `<link>` element.
     * @type {HTMLLinkElement | null}
     */
    const dark = document.getElementById('hljs-theme-dark');

    if (!light || !dark) return;

    /**
     * Reads the current theme from the `data-theme` attribute on `<html>`
     * and enables the matching highlight.js stylesheet while disabling the
     * other.
     *
     * @inner
     * @returns {void}
     */
    function apply() {
        const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
        light.disabled = isDark;
        dark.disabled  = !isDark;
    }

    // Synchronize immediately to match the theme present at script execution.
    apply();

    /**
     * Observes `data-theme` attribute mutations on `<html>` so `apply` is
     * called whenever the theme is switched by the toggle.
     *
     * @type {MutationObserver}
     */
    new MutationObserver(apply)
        .observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
})();