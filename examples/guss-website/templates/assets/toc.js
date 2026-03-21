'use strict';
/**
 * Builds and activates a Table of Contents for the current page.
 *
 * Queries all `h2` and `h3` elements inside `.prose` and generates a
 * nested link list inside `#toc`. If either container is absent, or if
 * no qualifying headings exist, the function exits immediately without
 * touching the DOM.
 *
 * Headings that lack an `id` attribute receive a generated one derived
 * from their text content via `slugify`, ensuring fragment links always
 * resolve correctly.
 *
 * `h3` links receive the `toc-h3` class so that CSS can indent them
 * relative to `h2` links to express the document hierarchy visually.
 *
 * An `IntersectionObserver` watches every heading and transfers the
 * `active` class to the corresponding TOC link as each heading crosses
 * the upper 40 % of the viewport (`rootMargin: '0px 0px -60% 0px'`),
 * so the highlighted entry always reflects the section currently being
 * read. The first link is marked active on load as a sensible default.
 */
(function () {
    /** @type {Element|null} The `.prose` content container to scan for headings. */
    const prose = document.querySelector('.prose');
    /** @type {HTMLElement|null} The `#toc` element that receives the generated link list. */
    const tocEl = document.getElementById('toc');
    if (!prose || !tocEl) {
        return;
    }

    /** @type {Element[]} All `h2` and `h3` elements found inside `.prose`, in DOM order. */
    const headings = Array.from(prose.querySelectorAll('h2, h3'));
    if (headings.length === 0) {
        return;
    }

    /**
     * Derives a URL-safe slug from a heading's text content.
     *
     * Converts the text to lower-case, replaces every run of
     * non-alphanumeric characters with a single hyphen, then strips
     * any leading or trailing hyphens from the result.
     *
     * @param {string} text - The raw text content of a heading element.
     * @returns {string} A hyphenated, lower-case slug suitable for use as an `id`.
     *
     * @example
     * slugify('Hello, World!')  // → 'hello-world'
     * slugify('  C++23  ')      // → 'c-23'
     */
    const slugify = (text) =>
        text.toLowerCase()
            .replace(/[^a-z0-9]+/g, '-')
            .replace(/^-|-$/g, '');

    /** @type {HTMLUListElement} Root list element appended to `#toc`. */
    const ul = document.createElement('ul');
    ul.className = 'list-none m-0 p-0 space-y-0.5';

    // Build one <li><a> pair per heading and collect them into the list.
    headings.forEach(h => {
        // Assign a generated id to any heading that lacks one so that
        // the fragment link created below always resolves.
        if (!h.id) {
            h.id = slugify(h.textContent || '');
        }

        const li = document.createElement('li');

        /** @type {HTMLAnchorElement} Fragment link pointing at this heading. */
        const a = document.createElement('a');
        a.href = '#' + h.id;
        a.textContent = h.textContent || '';
        // h3 links receive an extra class for CSS-driven indentation.
        a.className = h.tagName === 'H3' ? 'toc-h3' : '';
        li.appendChild(a);
        ul.appendChild(li);
    });

    tocEl.appendChild(ul);

    /** @type {HTMLAnchorElement[]} All generated TOC links, in DOM order. */
    const links = Array.from(ul.querySelectorAll('a'));

    /** @type {HTMLAnchorElement|null} The currently highlighted TOC link. */
    let activeLink = links[0] || null;

    // Mark the first entry active immediately so there is always a
    // highlighted link before the user starts scrolling.
    if (activeLink) {
        activeLink.classList.add('active');
    }

    /**
     * `IntersectionObserver` that transfers the `active` class to the TOC
     * link corresponding to whichever heading has just entered the reading
     * zone (the upper 40 % of the viewport).
     *
     * Options:
     * - `rootMargin: '0px 0px -60% 0px'` — shrinks the observable area to
     *   the top 40 % of the viewport, so a heading is considered "active"
     *   once it reaches the area where the reader's eye naturally rests.
     * - `threshold: 0` — fire as soon as any part of the heading enters
     *   the reduced observation area.
     */
    const observer = new IntersectionObserver(entries =>  entries.forEach(
        entry => {
            if (entry.isIntersecting) {
                const id = entry.target.id;
                /** @type {HTMLAnchorElement|null} */
                const link = ul.querySelector('a[href="#' + id + '"]');
                if (link && link !== activeLink) {
                    if (activeLink) {
                        activeLink.classList.remove('active');
                    }
                    activeLink = link;
                    activeLink.classList.add('active');
                }
            }
        }
    ), {
        rootMargin: '0px 0px -60% 0px',
        threshold: 0,
    });

    headings.forEach(h => observer.observe(h));
})();