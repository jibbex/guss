/**
 * Initializes the reading progress bar at the top of the page.
 *
 * The width of the `#progress-bar` element is updated on scroll to reflect
 * the user's progress through the article. The progress is calculated as
 * the ratio of `window.scrollY` to the total scrollable height of the
 * document, and is expressed as a percentage width for the progress bar.
 */
(function initReadingProgress() {
    const bar = document.getElementById('progress-bar');
    if (!bar) {
        return;
    }
    /**
     * Updates the width of the progress bar to reflect the current scroll position.
     *
     * Calculates the ratio of `window.scrollY` to the total scrollable distance
     * (`scrollHeight - innerHeight`) and expresses it as a percentage width on
     * the bar element. When the document is not scrollable (i.e. `docHeight` is
     * zero or negative), the bar is set to `0%` to avoid division by zero.
     */
    function update() {
        const scrollTop = window.scrollY;
        const docHeight = document.documentElement.scrollHeight - window.innerHeight;
        bar.style.width = docHeight > 0 ? (scrollTop / docHeight * 100) + '%' : '0%';
    }
    window.addEventListener('scroll', update, { passive: true });
    update();
})();