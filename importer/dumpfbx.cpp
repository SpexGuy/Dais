//
// Created by Martin Wickham on 8/26/17.
//

#include <cstdio>
#include <cstring>
#include "dumpfbx.h"

using namespace ofbx;

const int indentation = 2;
const size_t elbuffersize = 256;

static void catProperty(char str[elbuffersize], const IElementProperty *prop) {
    char tmp[elbuffersize];
    IElementProperty::Type type = prop->getType();
    DataView value = prop->getValue();
    switch (type) {
        case IElementProperty::DOUBLE: snprintf(tmp, elbuffersize, "%5f", value.toDouble()); break;
        case IElementProperty::LONG: snprintf(tmp, elbuffersize, "%lld", value.toLong()); break;
        case IElementProperty::INTEGER: snprintf(tmp, elbuffersize, "%d", *(int*)value.begin); break;
        case IElementProperty::STRING: prop->getValue().toString(tmp); break;
        default: snprintf(tmp, elbuffersize, "Type: %c [%d bytes]", type, int(value.end - value.begin));
    }
    strncat(str, tmp, elbuffersize - strlen(str) - 1);
}

// This has the schlemiel the painter problem, but it should be fast enough regardless.
void dumpElement(FILE *file, const IElement *e, int indent) {
    char str[elbuffersize];
    int c = 0;
    for (; c < indent; c++) {
        str[c] = ' ';
    }
    e->getID().toString(str + c, elbuffersize - c); // adds a null at the end
    const IElementProperty *prop = e->getFirstProperty();
    if (prop) {
        strncat(str, " (", elbuffersize - strlen(str) - 1);
        catProperty(str, prop);
        prop = prop->getNext();
        while (prop) {
            strncat(str, ", ", elbuffersize - strlen(str) - 1);
            catProperty(str, prop);
            prop = prop->getNext();
        }
        strncat(str, ")", elbuffersize - strlen(str) - 1);
    }
    fprintf(file, "%s\n", str);
}

void dumpElementRecursive(FILE *file, const IElement *e, int indent) {
    while (e) {
        dumpElement(file, e, indent);
        dumpElementRecursive(file, e->getFirstChild(), indent + indentation);
        e = e->getSibling();
    }
}

void dumpElements(const IScene *scene) {
    FILE *file = fopen("elements.out", "w");
    if (file == nullptr) {
        printf("Could not open file 'elements.out' (fopen returned nullptr). Continuing.");
        return;
    }

    printf("Dumping FBX Element tree to elements.out\n");
    fprintf(file, "Elements:\n");
    const IElement *rootElement = scene->getRootElement();
    dumpElementRecursive(file, rootElement);
    fclose(file);
}

void dumpObject(FILE *file, const Object *obj, int indent) {
    char str[elbuffersize];

    const char* label;
    switch (obj->getType())
    {
        case Object::Type::GEOMETRY: label = "geometry"; break;
        case Object::Type::MESH: label = "mesh"; break;
        case Object::Type::MATERIAL: label = "material"; break;
        case Object::Type::ROOT: label = "root"; break;
        case Object::Type::TEXTURE: label = "texture"; break;
        case Object::Type::NULL_NODE: label = "null"; break;
        case Object::Type::LIMB_NODE: label = "limb node"; break;
        case Object::Type::NODE_ATTRIBUTE: label = "node attribute"; break;
        case Object::Type::CLUSTER: label = "cluster"; break;
        case Object::Type::SKIN: label = "skin"; break;
        case Object::Type::ANIMATION_STACK: label = "animation stack"; break;
        case Object::Type::ANIMATION_LAYER: label = "animation layer"; break;
        case Object::Type::ANIMATION_CURVE: label = "animation curve"; break;
        case Object::Type::ANIMATION_CURVE_NODE: label = "animation curve node"; break;
        default: label = "unknown"; break;
    }

    int c;
    for (c = 0; c < indent; c++) {
        str[c] = ' ';
    }
    str[c] = '\0';
    strncat(str, label, elbuffersize - indent - 1);

    if (obj->isNode()) {
        fprintf(file, "%s N %s\n", str, obj->name);
    } else {
        fprintf(file, "%s %s\n", str, obj->name);
    }
}

void dumpObjectRecursive(FILE *file, const Object *obj, int indent) {
    dumpObject(file, obj, indent);
    //dumpElement(file, &obj->element, indent);
    if (obj->isNode()) {
        const Object *child;
        for (int i = 0; (child = obj->resolveObjectLink(i)); i++) {
            dumpObjectRecursive(file, child, indent + indentation);
        }
    }
}

void dumpObjects(const IScene *scene) {
    FILE *file = fopen("objects.out", "w");
    if (file == nullptr) {
        printf("Could not open file 'objects.out' (fopen returned nullptr). Continuing.");
        return;
    }

    printf("Dumping FBX Object tree to objects.out\n");
    fprintf(file, "Objects:\n");
    const Object *rootElement = scene->getRoot();
    dumpObjectRecursive(file, rootElement);
    fclose(file);
}


void dumpNodeRecursive(FILE *file, const Object *obj, int indent) {
    dumpObject(file, obj, indent);
    const Object *child;
    for (int i = 0; (child = obj->resolveObjectLink(i)); i++) {
        if (child->isNode()) {
            dumpNodeRecursive(file, child, indent + indentation);
        }
    }
}

void dumpNodes(const IScene *scene) {
    FILE *file = fopen("nodes.out", "w");
    if (file == nullptr) {
        printf("Could not open file 'nodes.out' (fopen returned nullptr). Continuing.");
        return;
    }

    printf("Dumping FBX Node tree to nodes.out\n");
    fprintf(file, "Nodes:\n");
    const Object *rootElement = scene->getRoot();
    dumpNodeRecursive(file, rootElement);
    fclose(file);
}
