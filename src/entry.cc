/*
 * NOTICE
 *
 * Copyright (c) 2016 David C Vernet and Matthew J Perron. All rights reserved.
 *
 * Unless otherwise noted, all of the material in this file is Copyright (c) 2016
 * by David C Vernet and Matthew J Perron. All rights reserved. No part of this file
 * may be reproduced, published, distributed, displayed, performed, copied,
 * stored, modified, transmitted or otherwise used or viewed by anyone other
 * than the authors (David C Vernet and Matthew J Perron),
 * for either public or private use.
 *
 * No part of this file may be modified, changed, exploited, or in any way
 * used for derivative works or offered for sale without the express
 * written permission of the authors.
 *
 * This file has been modified from lightweight-java-profiler
 * (https://github.com/dcapwell/lightweight-java-profiler). See APACHE_LICENSE for
 * a copy of the license that was included with that original work.
 */

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "globals.h"
#include "profiler.h"
#include "stacktraces.h"

static Profiler *prof;
FILE *Globals::OutFile;
static bool updateEventsEnabledState(jvmtiEnv *jvmti, jvmtiEventMode enabledState);
static volatile int class_prep_lock = 0;
static bool acquireCreateLock(); static void releaseCreateLock();

void JNICALL OnThreadStart(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                           jthread thread) {
    IMPLICITLY_USE(jvmti_env);
    IMPLICITLY_USE(thread);
    Accessors::SetCurrentJniEnv(jni_env);

    prof->addUserThread(thread);
}

void JNICALL OnThreadEnd(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(jni_env);
  IMPLICITLY_USE(thread);

  prof->removeUserThread(thread);
}

// This has to be here, or the VM turns off class loading events.
// And AsyncGetCallTrace needs class loading events to be turned on!
void JNICALL OnClassLoad(jvmtiEnv *jvmti_env, JNIEnv *jni_env, jthread thread,
                         jclass klass) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(jni_env);
  IMPLICITLY_USE(thread);
  IMPLICITLY_USE(klass);
}

// Create a java thread -- currently used
// to run profiler thread
jthread create_thread(JNIEnv *jni_env) {
    jclass cls = jni_env->FindClass("java/lang/Thread");
    if( cls == NULL ) {
        exit(1);
    }
    const char *method = "<init>";
    jmethodID methodId = jni_env->GetMethodID(cls, method, "()V");

    if( methodId == NULL ) {
        exit(1);
    }

    jobject ret = jni_env->NewObject(cls, methodId);

    return (jthread)ret;
}

/**
 * Either enable or disable the custom agent events. This is
 * fired when {@code startProfilingNative} or {@code endProfilingNative}
 * are called.
 */
static bool updateEventsEnabledState(jvmtiEnv *jvmti, jvmtiEventMode enabledState) {
  JVMTI_ERROR_1(
    (jvmti->SetEventNotificationMode(enabledState, JVMTI_EVENT_CLASS_PREPARE, NULL)),
    false);

  return true;
}

static bool acquireCreateLock() {
  bool has_lock = class_prep_lock == pthread_self();
  if (!has_lock) {
  	while (!__sync_bool_compare_and_swap(&class_prep_lock, 0, pthread_self()))
			;

		std::atomic_thread_fence(std::memory_order_acquire);
  }
	
  return !has_lock;
}

static void releaseCreateLock() {
	class_prep_lock = 0;
  std::atomic_thread_fence(std::memory_order_release);
}


// Calls GetClassMethods on a given class to force the creation of
// jmethodIDs of it.
void CreateJMethodIDsForClass(jvmtiEnv *jvmti, jclass klass) {
  if (!prof->isRunning()){
		return;
  }
  auto logger = prof->getLogger();
  bool releaseLock = acquireCreateLock();
  jint method_count;
  JvmtiScopedPtr<jmethodID> methods(jvmti);
  jvmtiError e = jvmti->GetClassMethods(klass, &method_count, methods.GetRef());
  if (e != JVMTI_ERROR_NONE && e != JVMTI_ERROR_CLASS_NOT_PREPARED) {
    // JVMTI_ERROR_CLASS_NOT_PREPARED is okay because some classes may
    // be loaded but not prepared at this point.
    JvmtiScopedPtr<char> ksig(jvmti);
    JVMTI_ERROR((jvmti->GetClassSignature(klass, ksig.GetRef(), NULL)));
    fprintf(
        stderr,
        "Failed to create method IDs for methods in class %s with error %d ",
        ksig.Get(), e);
  } else {
      JvmtiScopedPtr<char> ksig(jvmti);
      jvmti->GetClassSignature(klass, ksig.GetRef(), NULL);

      std::string package_str = "L" + prof->getPackage();
      std::string class_pkg_str = "Creating JMethod IDs for class: " + std::string(ksig.Get()) + " for scope: " + package_str;
      logger->info(class_pkg_str.c_str());
      if( strstr(ksig.Get(), package_str.c_str()) == ksig.Get() ) {
          prof->addInScopeMethods(method_count, methods.Get());

      }

      //TODO: this matches a prefix. class name AA will match a progress
      // point set with class A
      std::string progress_pt_str = "L" + prof->getProgressClass();
      if( strstr(ksig.Get(), progress_pt_str.c_str()) == ksig.Get() ) {
          prof->addProgressPoint(method_count, methods.Get());
      }
  }
  if (releaseLock) {
    releaseCreateLock();
  }
}

jint JNICALL startProfilingNative(JNIEnv *env, jobject thisObj) {
   printf("start profiling Native\n");
   fflush(stdout);
   // Forces the creation of jmethodIDs of the classes that had already
   // been loaded (eg java.lang.Object, java.lang.ClassLoader) and
   // OnClassPrepare() misses.
   jvmtiEnv * jvmti = prof->getJVMTI();
   jint class_count;
   JvmtiScopedPtr<jclass> classes(jvmti);
   prof->setJNI(env);
   prof->setMBeanObject(thisObj);
   prof->Start();
   updateEventsEnabledState(jvmti, JVMTI_ENABLE);
   jvmti->GetLoadedClasses(&class_count, classes.GetRef());
   jclass *classList = classes.Get();
   for (int i = 0; i < class_count; ++i) {
      jclass klass = classList[i];
      JvmtiScopedPtr<char> ksig(jvmti);
      jvmti->GetClassSignature(klass, ksig.GetRef(), NULL);
      printf("start prof class load :%s\n", ksig.Get());
      fflush(stdout);
      CreateJMethodIDsForClass(jvmti, klass);
   }

   jthread agent_thread = create_thread(env);
   jvmtiError agentErr = jvmti->RunAgentThread(agent_thread, &Profiler::runAgentThread, NULL, 1);
   return 0;
}

jint JNICALL endProfilingNative(JNIEnv *env, jobject thisObj) {
   printf("end Profiling native\n");
   fflush(stdout);
   prof->Stop();
   updateEventsEnabledState(prof->getJVMTI(), JVMTI_DISABLE);
   prof->clearMBeanObject();
   prof->clearProgressPoint();
   return 0;
}

jint JNICALL setProgressPointNative(JNIEnv *env, jobject thisObj, jstring className, jint line_no) {
  const char *nativeClassName = env->GetStringUTFChars(className, 0);
  printf("set Progress point native %s:%d\n", nativeClassName, line_no);
  fflush(stdout);
  prof->setProgressPoint(nativeClassName, line_no);


   // use your string
  env->ReleaseStringUTFChars(className, nativeClassName);
  return 0;
}

jint JNICALL setScopeNative(JNIEnv *env, jobject thisObj, jstring scope) {
  const char *nativeScope = env->GetStringUTFChars(scope, 0);

  prof->setScope(nativeScope);
  printf("set scope %s\n", nativeScope);

  fflush(stdout);
  env->ReleaseStringUTFChars(scope, nativeScope);
  return 0;
}



void JNICALL OnVMInit(jvmtiEnv *jvmti, JNIEnv *jni_env, jthread thread) {
  IMPLICITLY_USE(thread);
  IMPLICITLY_USE(jni_env);


  // register mbean

  jclass cls = jni_env->FindClass("com/vernetperronllc/jcoz/agent/JCozProfiler");
  if (cls == nullptr){
    fprintf(stderr, "Could not find JCoz Profiler class, did you add the jar to the classpath?\n");
    return;
  }
  jmethodID mid = jni_env->GetStaticMethodID(cls, "registerProfilerWithMBeanServer", "()V");
  if (mid == nullptr){
    fprintf(stderr, "Could not find static method to register the mbean.\n");
    return;
  }

  JNINativeMethod methods[] = {
       {(char *)"startProfilingNative",   (char *)"()I",                     (void *)&startProfilingNative},
       {(char *)"endProfilingNative",     (char *)"()I",                     (void *)&endProfilingNative},
       {(char *)"setProgressPointNative", (char *)"(Ljava/lang/String;I)I",  (void *)&setProgressPointNative},
       {(char *)"setScopeNative",         (char *)"(Ljava/lang/String;)I",   (void *)&setScopeNative},
   };

  jint err;
  err = jni_env->RegisterNatives(cls, methods, sizeof(methods)/sizeof(JNINativeMethod));
  if (err != JVMTI_ERROR_NONE){
    fprintf(stderr, "Could not register natives with error %d\n", err);
    return;
  }
  jni_env->CallStaticVoidMethod(cls, mid);
}

void JNICALL OnClassPrepare(jvmtiEnv *jvmti_env, JNIEnv *jni_env,
                            jthread thread, jclass klass) {
  IMPLICITLY_USE(jni_env);
  IMPLICITLY_USE(thread);
  // We need to do this to "prime the pump", as it were -- make sure
  // that all of the methodIDs have been initialized internally, for
  // AsyncGetCallTrace.  I imagine it slows down class loading a mite,
  // but honestly, how fast does class loading have to be?
  CreateJMethodIDsForClass(jvmti_env, klass);
}

void JNICALL OnVMDeath(jvmtiEnv *jvmti_env, JNIEnv *jni_env) {
  IMPLICITLY_USE(jvmti_env);
  IMPLICITLY_USE(jni_env);

//  prof->printInScopeLineNumberMapping();

  // prof->clearProgressPoint();
  prof->Stop();

}

static bool PrepareJvmti(jvmtiEnv *jvmti) {
  // Set the list of permissions to do the various internal VM things
  // we want to do.
  jvmtiCapabilities caps;

  memset(&caps, 0, sizeof(caps));
  caps.can_generate_all_class_hook_events = 1;

  caps.can_get_source_file_name = 1;
  caps.can_get_line_numbers = 1;
  caps.can_get_bytecodes = 1;
  caps.can_get_constant_pool = 1;
  caps.can_generate_breakpoint_events = 1;

  jvmtiCapabilities all_caps;
  memset(&all_caps, 0, sizeof(all_caps));
  int error;

  if (JVMTI_ERROR_NONE ==
      (error = jvmti->GetPotentialCapabilities(&all_caps))) {
    // This makes sure that if we need a capability, it is one of the
    // potential capabilities.  The technique isn't wonderful, but it
    // This makes sure that if we need a capability, it is one of the
    // potential capabilities.  The technique isn't wonderful, but it
    // is compact and as likely to be compatible between versions as
    // anything else.
    char *has = reinterpret_cast<char *>(&all_caps);
    const char *should_have = reinterpret_cast<const char *>(&caps);
    for (int i = 0; i < sizeof(all_caps); i++) {
      if ((should_have[i] != 0) && (has[i] == 0)) {
        return false;
      }
    }

    // This adds the capabilities.
    if ((error = jvmti->AddCapabilities(&caps)) != JVMTI_ERROR_NONE) {
      fprintf(stderr, "Failed to add capabilities with error %d\n", error);
      return false;
    }
  }
  return true;
}

static bool RegisterJvmti(jvmtiEnv *jvmti) {
  // Create the list of callbacks to be called on given events.
  jvmtiEventCallbacks *callbacks = new jvmtiEventCallbacks();
  memset(callbacks, 0, sizeof(jvmtiEventCallbacks));

  callbacks->ThreadStart = &OnThreadStart;
  callbacks->ThreadEnd = &OnThreadEnd;
  callbacks->VMInit = &OnVMInit;
  callbacks->VMDeath = &OnVMDeath;

  callbacks->ClassLoad = &OnClassLoad;
  callbacks->ClassPrepare = &OnClassPrepare;
  callbacks->Breakpoint = &(Profiler::HandleBreakpoint);

  JVMTI_ERROR_1(
      (jvmti->SetEventCallbacks(callbacks, sizeof(jvmtiEventCallbacks))),
      false);

  jvmtiEvent events[] = {JVMTI_EVENT_CLASS_LOAD, JVMTI_EVENT_BREAKPOINT,
                         JVMTI_EVENT_THREAD_END, JVMTI_EVENT_THREAD_START,
                         JVMTI_EVENT_VM_DEATH, JVMTI_EVENT_VM_INIT};

  size_t num_events = sizeof(events) / sizeof(jvmtiEvent);

  // Enable the callbacks to be triggered when the events occur.
  // Events are enumerated in jvmstatagent.h
  for (int i = 0; i < num_events; i++) {
    JVMTI_ERROR_1(
        (jvmti->SetEventNotificationMode(JVMTI_ENABLE, events[i], NULL)),
        false);
  }

  return true;
}

#define POSITIVE(x) (static_cast<size_t>(x > 0 ? x : 0))

static void SetFileFromOption(char *equals) {
  char *name_begin = equals + 1;
  char *name_end;
  if ((name_end = strchr(equals, ',')) == NULL) {
    name_end = equals + strlen(equals);
  }
  size_t len = POSITIVE(name_end - name_begin);
  char *file_name = new char[len];
  strncpy(file_name, name_begin, len);
  if (strcmp(file_name, "stderr") == 0) {
    Globals::OutFile = stderr;
  } else if (strcmp(file_name, "stdout") == 0) {
    Globals::OutFile = stdout;
  } else {
    Globals::OutFile = fopen(file_name, "w+");
    if (Globals::OutFile == NULL) {
      fprintf(stderr, "Could not open file %s: ", file_name);
      perror(NULL);
      exit(1);
    }
  }

  delete[] file_name;
}

AGENTEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *options,
                                      void *reserved) {
  IMPLICITLY_USE(reserved);
  int err;
  jvmtiEnv *jvmti;

  Accessors::Init();

  if ((err = (vm->GetEnv(reinterpret_cast<void **>(&jvmti), JVMTI_VERSION))) != JNI_OK ) {
    return 1;
  }



//  // Read the command line options and set progress points and package
//  prof->ParseOptions(options);

  if (!PrepareJvmti(jvmti)) {
    fprintf(stderr, "Failed to initialize JVMTI.  Continuing...\n");
    return 0;
  }

  if (!RegisterJvmti(jvmti)) {
    fprintf(stderr, "Failed to enable JVMTI events.  Continuing...\n");
    // We fail hard here because we may have failed in the middle of
    // registering callbacks, which will leave the system in an
    // inconsistent state.
    return 1;
  }

  Asgct::SetAsgct(Accessors::GetJvmFunction<ASGCTType>("AsyncGetCallTrace"));

  prof = new Profiler(jvmti);

  prof->setJVMTI(jvmti);
  prof->init();

  return 0;
}

AGENTEXPORT void JNICALL Agent_OnUnload(JavaVM *vm) {
  IMPLICITLY_USE(vm);
  Accessors::Destroy();
}
