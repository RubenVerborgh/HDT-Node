#include <assert.h>
#include <vector>
#include "HdtDocument.h"

using namespace v8;
using namespace hdt;

// Arguments for CreateAsync
typedef struct CreateArgs {
  string filename;
  HDT* hdt;
  string error;
  Persistent<Function> callback;

  CreateArgs(char* filename, Persistent<Function> callback)
    : filename(filename), hdt(NULL), callback(callback) { };
} CreateArgs;

// Creates a new instance of HdtDocument.
// JavaScript signature: createHdtDocument(filename, callback)
Handle<Value> HdtDocument::CreateAsync(const Arguments& args) {
  HandleScope scope;
  assert(args.Length() == 2);

  // Create asynchronous task
  uv_work_t *request = new uv_work_t;
  request->data = new CreateArgs(*String::Utf8Value(args[0]),
      Persistent<Function>::New(Local<Function>::Cast(args[1])));
  uv_queue_work(uv_default_loop(), request, HdtDocument::Create, HdtDocument::CreateDone);
  return scope.Close(Undefined());
}

// Performs the creation of the HDT document's internals
void HdtDocument::Create(uv_work_t *request) {
  CreateArgs* args = (CreateArgs*)request->data;
  try { args->hdt = HDTManager::mapIndexedHDT(args->filename.c_str()); }
  catch (const char* error) { args->error = string(error); }
}

// Sends the result of Create through a callback.
void HdtDocument::CreateDone(uv_work_t *request, const int status) {
  CreateArgs* args = (CreateArgs*)request->data;

  // Send new HdtDocument object (or error) through the callback
  const unsigned argc = 2;
  Handle<Value> argv[argc] = { Null(), Undefined() };
  if (args->hdt) {
    // Create new HDT document
    Local<Object> newDocument = constructor->NewInstance(0, NULL);
    argv[1] = newDocument;
    // Set the HDT instance, which was created asynchronously by Create
    HdtDocument* hdtDocument = ObjectWrap::Unwrap<HdtDocument>(newDocument);
    hdtDocument->hdt = args->hdt;
  }
  else {
    // HDT instance creation was unsuccessful; send error
    argv[0] = Exception::Error(String::New(args->error.c_str()));
  }
  args->callback->Call(Context::GetCurrent()->Global(), argc, argv);

  // Delete objects used during the creation
  args->callback.Dispose();
  delete args;
  delete request;
}

// Creates a new document.
HdtDocument::HdtDocument() : hdt(NULL) { }

// Deletes the document.
HdtDocument::~HdtDocument() {
  Destroy();
}

// Destroys the document, disabling all further operations.
void HdtDocument::Destroy() {
  if (hdt) {
    delete hdt;
    hdt = NULL;
  }
}

// Creates the constructor of HdtDocument.
Persistent<Function> HdtDocument::CreateConstructor() {
  // Create constructor template
  Local<FunctionTemplate> constructorTemplate = FunctionTemplate::New(New);
  constructorTemplate->SetClassName(String::NewSymbol("HdtDocument"));
  constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);
  // Create prototype
  Local<v8::ObjectTemplate> prototypeTemplate = constructorTemplate->PrototypeTemplate();
  prototypeTemplate->Set(String::NewSymbol("_search"), FunctionTemplate::New(SearchAsync)->GetFunction());
  prototypeTemplate->Set(String::NewSymbol("close"),   FunctionTemplate::New(Close) ->GetFunction());
  prototypeTemplate->SetAccessor(String::NewSymbol("closed"), ClosedGetter, NULL);
  return Persistent<Function>::New(constructorTemplate->GetFunction());
}

// Constructor of HdtDocument.
Persistent<Function> HdtDocument::constructor = HdtDocument::CreateConstructor();

// Constructs an HdtDocument wrapper.
Handle<Value> HdtDocument::New(const Arguments& args) {
  HandleScope scope;
  assert(args.IsConstructCall());

  HdtDocument* hdtDocument = new HdtDocument();
  hdtDocument->Wrap(args.This());
  return args.This();
}

// Arguments for SearchAsync
typedef struct SearchArgs {
  HdtDocument* hdtDocument;
  string subject, predicate, object;
  Persistent<Function> callback;
  vector<TripleString*> triples;

  SearchArgs(HdtDocument* hdtDocument, char* subject, char* predicate, char* object,
             Persistent<Function> callback)
    : hdtDocument(hdtDocument), subject(subject), predicate(predicate), object(object),
      callback(callback) { };
} SearchArgs;

// Searches for a triple pattern in the document.
// JavaScript signature: HdtDocument#_search(subject, predicate, object, callback)
Handle<Value> HdtDocument::SearchAsync(const Arguments& args) {
  HandleScope scope;
  assert(args.Length() == 4);

  // Create asynchronous task
  uv_work_t *request = new uv_work_t;
  request->data = new SearchArgs(ObjectWrap::Unwrap<HdtDocument>(args.This()),
      *String::Utf8Value(args[0]), *String::Utf8Value(args[1]), *String::Utf8Value(args[2]),
      Persistent<Function>::New(Local<Function>::Cast(args[3])));
  uv_queue_work(uv_default_loop(), request, HdtDocument::Search, HdtDocument::SearchDone);
  return scope.Close(Undefined());
}

// Performs the search for a triple pattern for SearchAsync.
void HdtDocument::Search(uv_work_t *request) {
  // Search the HDT document
  SearchArgs* args = (SearchArgs*)request->data;
  IteratorTripleString *it = args->hdtDocument->
    hdt->search(args->subject.c_str(), args->predicate.c_str(), args->object.c_str());

  // Add the triples to the result vector
  while (it->hasNext())
    args->triples.push_back(new TripleString(*it->next()));
  delete it;
}

// Sends the result of Search through a callback.
void HdtDocument::SearchDone(uv_work_t *request, const int status) {
  // Convert the found triples into a JavaScript object array
  SearchArgs* args = (SearchArgs*)request->data;
  Handle<Array> triples = Array::New(args->triples.size());
  long count = 0;
  for (vector<TripleString*>::iterator it = args->triples.begin(); it != args->triples.end(); it++) {
    Handle<Object> tripleObject = Object::New();
    tripleObject->Set(String::NewSymbol("subject"),   String::New((*it)->getSubject()  .c_str()));
    tripleObject->Set(String::NewSymbol("predicate"), String::New((*it)->getPredicate().c_str()));
    tripleObject->Set(String::NewSymbol("object"),    String::New((*it)->getObject()   .c_str()));
    triples->Set(count++, tripleObject);
    delete *it;
  }

  // Send the JavaScript array through the callback
  const unsigned argc = 2;
  Handle<Value> argv[argc] = { Null(), triples };
  args->callback->Call(Context::GetCurrent()->Global(), argc, argv);

  // Delete objects used during the search
  args->callback.Dispose();
  delete args;
  delete request;
}

// Closes the document, disabling all further operations.
// JavaScript signature: HdtDocument#close([callback])
Handle<Value> HdtDocument::Close(const Arguments& args) {
  HandleScope scope;

  // Destroy the current document
  HdtDocument* hdtDocument = ObjectWrap::Unwrap<HdtDocument>(args.This());
  hdtDocument->Destroy();

  // Call the callback if one was passed
  if (args.Length() >= 1 && args[0]->IsFunction()) {
    const Local<Function> callback = Local<Function>::Cast(args[0]);
    const unsigned argc = 1;
    Handle<Value> argv[argc] = { Null() };
    callback->Call(Context::GetCurrent()->Global(), argc, argv);
  }
  return scope.Close(Undefined());
}

// Gets the version of the module.
Handle<Value> HdtDocument::ClosedGetter(Local<String> property, const AccessorInfo& info) {
  HandleScope scope;
  HdtDocument* hdtDocument = ObjectWrap::Unwrap<HdtDocument>(info.This());
  return scope.Close(Boolean::New(!hdtDocument->hdt));
}