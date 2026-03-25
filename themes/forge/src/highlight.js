'use strict';
import hljs from 'highlight.js';

hljs.highlightAll();

(function syncHljs() {
    const light = document.getElementById('hljs-theme-light');
    const dark  = document.getElementById('hljs-theme-dark');
    if (!light || !dark) return;

    function apply() {
        const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
        light.disabled = isDark;
        dark.disabled  = !isDark;
    }

    apply();
    new MutationObserver(apply)
        .observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
})();
