/**
 * Initializes the sticky Table of Contents (TOC) sidebar for a post page.
 *
 * Queries all `h2` and `h3` elements inside `.prose`, builds a nested `<ul>`
 * link list inside `#toc-nav`, and keeps the link that corresponds to the
 * heading currently nearest the top of the viewport marked as `active`.
 *
 * Behavior overview:
 * - If `#toc-nav` does not exist the function exits immediately (e.g. on pages
 *   that include no TOC partial).
 * - If no qualifying headings are found, `#toc-aside` is hidden so the sidebar
 *   does not render empty.
 * - Headings that lack an `id` attribute receive a generated one
 *   (`heading-0`, `heading-1`, …) so fragment links always resolve.
 * - `h2` links are rendered at `0.8 rem`; `h3` links are indented and rendered
 *   at `0.72 rem` to express the visual hierarchy.
 * - Clicking a TOC link scrolls smoothly to the target heading, sets it active
 *   immediately, and suppresses scroll-driven active updates for `DELAY` ms
 *   to prevent the active indicator from jumping during the animation.
 * - On every scroll event the heading whose top edge is closest to but still
 *   above `OFFSET` px from the viewport top is marked active.
 */
(function initTOC() {
    /** @type {HTMLElement | null} The `<nav>` container that receives the generated link list. */
    const nav = document.getElementById('toc-nav');
    if (!nav) {
        return;
    }

    /** @type {HTMLElement[]} All `h2` and `h3` elements inside the `.prose` content area. */
    const headings = Array.from(document.querySelectorAll('.prose h2, .prose h3'));
    if (!headings.length) {
        document.getElementById('toc-aside').style.display = 'none';
        return;
    }

    // Ensure every heading has a stable `id` for fragment navigation.
    // Headings that already carry an author-supplied `id` are left untouched.
    headings.forEach((h, i) => {
        if (!h.id) {
            h.id = 'heading-' + i;
        }
    });

    /** @type {HTMLUListElement} The root list element appended to `#toc-nav`. */
    const ul = document.createElement('ul');
    ul.className = 'space-y-1';

    // Build one <li><a> pair for every heading and append it to the list.
    headings.forEach(h => {
        const li = document.createElement('li');
        const a= document.createElement('a');
        a.href = '#' + h.id;
        a.textContent = h.textContent;
        a.dataset.headingId = h.id;
        // Compose Tailwind classes: h3 links are indented and use a smaller font size.
        a.className = [
            'toc-link',
            h.tagName === 'H3' ? 'pl-6 text-[0.72rem]' : 'text-[0.8rem]',
            'py-0.5 text-on-surface-muted dark:text-on-surface-dark-muted',
            'hover:text-on-surface-strong dark:hover:text-on-surface-dark-strong'
        ].join(' ');
        li.appendChild(a);
        ul.appendChild(li);
    });

    nav.appendChild(ul);

    /** @type {NodeListOf<HTMLAnchorElement>} All generated `.toc-link` anchors. */
    const links = nav.querySelectorAll('.toc-link');

    /**
     * Pixel distance from the top of the viewport used as the activation
     * threshold. A heading is considered "active" once its top edge has
     * scrolled above this line.
     *
     * @constant {number}
     */
    const OFFSET = 100;

    /**
     * Milliseconds to suppress scroll-driven TOC updates after a TOC link is
     * clicked. Prevents the active indicator from flickering while the smooth-
     * scroll animation is in progress.
     *
     * @constant {number}
     */
    const DELAY = 800;

    /** @type {string | null} The `id` of the currently active heading, or `null` if none. */
    let activeId = null;

    /** @type {boolean} `true` while a programmatic smooth-scroll is in progress. */
    let scrolling = false;

    /** @type {ReturnType<typeof setTimeout> | null} Handle for the scroll-lock reset timer. */
    let scrollTimer = null;

    /**
     * Marks the heading with the given `id` as active in the TOC.
     * No-ops when `id` already matches `activeId` to avoid redundant DOM writes.
     *
     * @param {string | null} id - The `id` of the heading to activate,
     *     or `null` to clear the active state.
     */
    function setActive(id) {
        if (id === activeId) {
            return;
        }
        activeId = id;
        links.forEach(
            a => a?.classList?.toggle('active', a.dataset.headingId === id)
        );
    }

    /**
     * Determines which heading is currently "in view" and updates the active
     * TOC link accordingly.
     *
     * Iterates the heading list in reverse order and activates the last heading
     * whose top edge is at or above `OFFSET` px from the viewport top. This
     * means the highest heading that has already scrolled past the threshold
     * is considered active. When no heading has crossed the threshold (i.e.
     * the reader is above the first heading), the active state is cleared.
     *
     * The function is a no-op while `scrolling` is `true` to avoid fighting
     * the smooth-scroll animation triggered by a TOC link click.
     */
    function updateTOC() {
        if (scrolling) {
            return;
        }
        let newId = null;
        for (let i = headings.length - 1; i >= 0; i--) {
            if (headings[i].getBoundingClientRect().top <= OFFSET) {
                newId = headings[i].id;
                break;
            }
        }
        setActive(newId);
    }

    // Wire up click handlers for each TOC link.
    links.forEach(a => {
        /**
         * Handles a TOC link click.
         *
         * Prevents the default anchor jump, immediately marks the target heading
         * active, locks scroll-driven updates for `DELAY` ms, then initiates a
         * smooth scroll to the heading. After the lock expires, `updateTOC` is
         * called once to reconcile the active state with the final scroll position.
         *
         * @param {MouseEvent} e - The click event.
         */
        a.addEventListener('click', e => {
            e.preventDefault();
            const target = document.getElementById(a.dataset.headingId);
            if (!target) {
                return;
            }
            setActive(a.dataset.headingId);
            scrolling = true;
            clearTimeout(scrollTimer);
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
            scrollTimer = setTimeout(() => {
                scrolling = false;
                updateTOC();
            }, DELAY);
        });
    });

    // Update the active link on every scroll event.
    // `passive: true` tells the browser this handler never calls preventDefault,
    // allowing it to optimize scroll performance.
    window.addEventListener('scroll', updateTOC, { passive: true });

    // Run once on load so the correct link is active when the page opens with
    // a fragment URL or after a back-navigation that restores scroll position.
    updateTOC();
})();