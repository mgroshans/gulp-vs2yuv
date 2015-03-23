﻿#ifndef VSJS_H
#define VSJS_H

#include <node.h>
#include <nan.h>
#include "VSHelper.h"
#include "VSScript.h"

class Vapoursynth : public node::ObjectWrap {
    public:
        static void Init(v8::Handle<v8::Object> exports);
        
    private:
        Vapoursynth(const char*, const char*);
        ~Vapoursynth();

        static NAN_METHOD(New);
        static NAN_METHOD(GetInfo);
        static NAN_METHOD(GetFrame);
        static v8::Persistent<v8::Function> constructor;
        const VSAPI *vsapi;
        VSScript *se;
        VSNodeRef *node;
        const VSVideoInfo *vi;
};

#endif