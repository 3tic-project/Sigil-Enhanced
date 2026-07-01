/************************************************************************
**
**  Copyright (C) 2026 3TIC-Project
**
**  This file is part of Sigil-Enhanced.
**
**  Sigil-Enhanced is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "Misc/ResourceInsertion.h"

#include <QFileInfo>

#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"

namespace
{
QString ResourceLabel(const Resource *resource)
{
    if (!resource) {
        return QString();
    }

    QString label = QFileInfo(resource->Filename()).completeBaseName();
    if (label.isEmpty()) {
        label = resource->Filename();
    }
    return Utility::EncodeXML(label);
}

QString RelativeHref(const Resource *resource, const Resource *relative_to_resource)
{
    if (!resource || !relative_to_resource) {
        return QString();
    }
    return Utility::EncodeXML(Utility::URLEncodePath(resource->GetRelativePathFromResource(relative_to_resource)));
}
}

namespace ResourceInsertion
{
const char * const BOOK_BROWSER_RESOURCE_MIME = "application/x-sigil-enhanced-book-browser-resources";

bool CanInsertResource(const Resource *resource, Context context)
{
    if (!resource) {
        return false;
    }

    const Resource::ResourceType type = resource->Type();

    if (context == Context::HTML) {
        return type == Resource::ImageResourceType ||
               type == Resource::SVGResourceType ||
               type == Resource::VideoResourceType ||
               type == Resource::AudioResourceType;
    }

    if (context == Context::CSS) {
        return type == Resource::ImageResourceType ||
               type == Resource::SVGResourceType ||
               type == Resource::FontResourceType;
    }

    return false;
}

bool ContextFromTargetResource(const Resource *target_resource, Context &context)
{
    if (!target_resource) {
        return false;
    }

    if (target_resource->Type() == Resource::HTMLResourceType) {
        context = Context::HTML;
        return true;
    }

    if (target_resource->Type() == Resource::CSSResourceType) {
        context = Context::CSS;
        return true;
    }

    return false;
}

QString TextForResource(const Resource *resource, const Resource *relative_to_resource, Context context)
{
    if (!CanInsertResource(resource, context) || !relative_to_resource) {
        return QString();
    }

    const QString href = RelativeHref(resource, relative_to_resource);
    if (href.isEmpty()) {
        return QString();
    }

    if (context == Context::CSS) {
        return QString("url(\"%1\")").arg(href);
    }

    const QString label = ResourceLabel(resource);
    switch (resource->Type()) {
        case Resource::ImageResourceType:
        case Resource::SVGResourceType:
            return QString("<img alt=\"%1\" src=\"%2\"/>").arg(label).arg(href);
        case Resource::VideoResourceType:
            return QString("<video controls=\"controls\" src=\"%1\">%2</video>").arg(href).arg(label);
        case Resource::AudioResourceType:
            return QString("<audio controls=\"controls\" src=\"%1\">%2</audio>").arg(href).arg(label);
        default:
            break;
    }

    return QString();
}
}
