Introduction
============

Starting with version 0.5.0, the user interface of Sigil supports being
displayed in multiple languages.


Web Tools
=========

Sigil can be translated using the web based translation system Transifex. See
https://www.transifex.com/zdpo/sigil/ . This is the preferred method for
translating Sigil.


Updating Base Translation File
==============================

NOTE: You need to delete base.ts first or it will get rebuilt in a broken 
manner for some reason (a bug in Qt?)

1. Make sure the Qt project "bin" directory is in your path so that
the right version of "lconvert" can be found. For me this is:

   export MYQTHOME=~/Qt6102
   export PATH=${MYQTHOME}/bin:${PATH}

2. Open a terminal and change to the Sigil/src/Resource_Files/ts directory.

3. rm base.ts

4. Run:
    lupdate ../../* -ts base.ts

5. Now manually edit the resulting base.ts file and change all of the 
following single numerusform tags:

    <numerusform></numerusform>

to be double empty numerusform tags as follows:

    <numerusform></numerusform><numerusform></numerusform>

so that our base.ts file will work with the much older tools used
on the Transifex site.



Naming convention
=================

All translations files should have the form sigil_lang.ts. Where lang is the
two letter language code. For example the Polish translation will have the
filename sigil_pl.ts and the German translation will have the filename
sigil_de.ts.


Simplified Chinese Coverage
===========================

The Simplified Chinese catalog is checked against the current C++, header, and
Qt Designer sources by the `zh_cn_translation_coverage` CTest. The check fails
when an active source is missing, unfinished, empty, stale, loses a `%1`/`%n`
placeholder, or changes the tag structure of a rich-text label. It also detects
high-confidence cases where visible text is passed directly to a Qt widget API
without `tr()`.

After changing user-visible text, update the catalog with the same Qt version
used to build Sigil:

    lupdate src -recursive -extensions cpp,h,ui \
        -ts src/Resource_Files/ts/sigil_zh_CN.ts

Translate every new active entry, preserve placeholders and rich-text tags, and
then run:

    ctest --test-dir cmake-build-debug \
        -R zh_cn_translation_coverage --output-on-failure

Names of products, file formats, standards, APIs, keyboard shortcuts, and code
identifiers may remain unchanged. Dialog text, buttons, labels, settings,
tooltips, status messages, report headers, and generated display names must be
translated.
