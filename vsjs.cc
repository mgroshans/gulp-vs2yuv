#include <sstream>

#include "vsjs.h"
#include "node_buffer.h"

using v8::Local;
using v8::Value;
using v8::Handle;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Function;
using v8::Persistent;
using v8::FunctionTemplate;

using node::Buffer::Data;

using std::ostringstream;

Persistent<Function> Vapoursynth::constructor;

Vapoursynth::Vapoursynth(const char *script, const char *path) {
    vsapi = vsscript_getVSApi();
    assert(vsapi);

    se = NULL;
    if (vsscript_evaluateScript(&se, script, path, efSetWorkingDir)) {
        const char *error = vsscript_getError(se);
        vsscript_freeScript(se);
        se = NULL;

        ostringstream ss;
        ss << "Failed to evaluate " << path;
        if (error) {
            ss << ": " << error;
        }

        NanThrowError(ss.str().data());
        return;
    }

    node = vsscript_getOutput(se, 0);
    if (!node) {
       vsscript_freeScript(se);
       se = NULL;
        NanThrowError("Failed to retrieve output node\n");
        return;
    }

    vi = vsapi->getVideoInfo(node);

    if (!isConstantFormat(vi) || !vi->numFrames) {
        vsapi->freeNode(node);
        vsscript_freeScript(se);
        se = NULL;
        NanThrowError("Cannot output clips with varying dimensions or unknown length\n");
        return;
    }
}

Vapoursynth::~Vapoursynth() {
    if (se != NULL) {
       vsscript_freeScript(se);
    }
}

void Vapoursynth::Init(Handle<Object> exports) {
    Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
    tpl->SetClassName(NanNew<String>("Vapoursynth"));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getInfo", GetInfo);
    NODE_SET_PROTOTYPE_METHOD(tpl, "getFrame", GetFrame);
    NanAssignPersistent<Function>(constructor, tpl->GetFunction());
    exports->Set(NanNew<String>("Vapoursynth"), tpl->GetFunction());

    if (!vsscript_init()) {
        NanThrowError("Failed to initialize Vapoursynth environment");
        return;
    }
}

NAN_METHOD(Vapoursynth::New) {
    NanEscapableScope();

    if (args.IsConstructCall()) {
        // Invoked as constructor: `new Vapoursynth(...)`
        const int32_t scriptLen = args[0].As<Object>()->Get(NanNew<String>("length"))->Int32Value();
        char *script = Data(args[0]);
        script[scriptLen] = '\0';
        const NanUtf8String utf8path(args[1]);
        const char *path(*utf8path);
        Vapoursynth *obj = new Vapoursynth(script, path);
        obj->Wrap(args.This());
        NanReturnValue(args.This());
    } else {
        // Invoked as plain function `Vapoursynth(...)`, turn into construct call
        const int argc = 2;
        Local<Value> argv[argc] = { args[0], args[1] };
        Local<Function> cons = NanNew<Function>(constructor);
        NanReturnValue(NanEscapeScope(cons->NewInstance(argc, argv)));
    }
}

class FrameWorker : public NanAsyncWorker {
    public:
        FrameWorker(Vapoursynth *obj, int32_t frameNumber, char *outBuffer, NanCallback *callback)
            : obj(obj), frameNumber(frameNumber), outBuffer(outBuffer), NanAsyncWorker(callback) {}
        ~FrameWorker() {}

    void Execute() {
        char errMsg[1024];
        const VSFrameRef *frame = obj->vsapi->getFrame(frameNumber, obj->node, errMsg, sizeof(errMsg));

        if (!frame) {
            ostringstream ss;
            ss << "Encountered error getting frame " << frameNumber << ": " << errMsg;
            SetErrorMessage(ss.str().data());
            return;
        }

        for (int p = 0; p < obj->vi->format->numPlanes; p++) {
            int stride = obj->vsapi->getStride(frame, p);
            const uint8_t *readPtr = obj->vsapi->getReadPtr(frame, p);
            int rowSize = obj->vsapi->getFrameWidth(frame, p) * obj->vi->format->bytesPerSample;
            int height = obj->vsapi->getFrameHeight(frame, p);

            for (int y = 0; y < height; y++) {
                memcpy(outBuffer, readPtr, rowSize);
                outBuffer += rowSize;
                readPtr += stride;
            }
        }

        obj->vsapi->freeFrame(frame);
    }

    private:
        Vapoursynth *obj;
        int32_t frameNumber;
        char *outBuffer;
};

NAN_METHOD(Vapoursynth::GetFrame) {
    NanScope();
    Vapoursynth *obj = Unwrap<Vapoursynth>(args.This());

    int32_t frameNumber = args[0]->Int32Value();
    char *outBuffer = Data(args[1]);
    NanCallback *callback = new NanCallback(args[2].As<Function>());

    NanAsyncQueueWorker(new FrameWorker(obj, frameNumber, outBuffer, callback));
}

static int frameSize(const VSVideoInfo *vi) {
    if (!vi) {
        return 0;
    }

    int frame_size = (vi->width * vi->format->bytesPerSample) >> vi->format->subSamplingW;
    if (frame_size) {
        frame_size *= vi->height;
        frame_size >>= vi->format->subSamplingH;
        frame_size *= 2;
    }
    frame_size += vi->width * vi->format->bytesPerSample * vi->height;

    return frame_size;
}

NAN_METHOD(Vapoursynth::GetInfo) {
    NanEscapableScope();
    Vapoursynth *obj = Unwrap<Vapoursynth>(args.This());

    Local<Object> ret = NanNew<Object>();
    ret->Set(NanNew<String>("height"), NanNew<Number>(obj->vi->height));
    ret->Set(NanNew<String>("width"), NanNew<Number>(obj->vi->width));
    ret->Set(NanNew<String>("numFrames"), NanNew<Number>(obj->vi->numFrames));

    Local<Object> fps = NanNew<Object>();
    fps->Set(NanNew<String>("numerator"), NanNew<Number>(obj->vi->fpsNum));
    fps->Set(NanNew<String>("denominator"), NanNew<Number>(obj->vi->fpsDen));
    ret->Set(NanNew<String>("fps"), fps);

    ret->Set(NanNew<String>("frameSize"), NanNew<Number>(frameSize(obj->vi)));

    NanReturnValue(NanEscapeScope(ret));
}