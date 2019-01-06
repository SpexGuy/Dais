//
// Created by Martin Wickham on 8/26/17.
//

#ifndef DUMPFBX_H_
#define DUMPFBX_H_

#include "openfbx/ofbx.h"

void dumpElements(const ofbx::IScene *scene);
void dumpElementRecursive(FILE *file, const ofbx::IElement *element, int indent = 2);
void dumpElement(FILE *file, const ofbx::IElement *element, int indent = 2);

void dumpObjects(const ofbx::IScene *scene);
void dumpObjectRecursive(FILE *file, const ofbx::Object *obj, int indent = 2);
void dumpObject(FILE *file, const ofbx::Object *obj, int indent = 2);

void dumpNodes(const ofbx::IScene *scene);
void dumpNodeRecursive(FILE *file, const ofbx::Object *obj, int indent = 2);

#endif
