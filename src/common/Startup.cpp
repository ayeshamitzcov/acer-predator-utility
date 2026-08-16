#include "common/Startup.h"

#include "common/Log.h"

#include <objbase.h>
#include <windows.h>
#include <taskschd.h>

#include <string>

namespace predator {
namespace {

constexpr wchar_t kTaskName[] = L"PredatorUtility";

std::wstring ExePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

void Release(IUnknown* p) {
    if (p) {
        p->Release();
    }
}

}  // namespace

bool SetRunAtStartup(bool enable, bool minimized) {
    HRESULT hr_init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ITaskService* svc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                  reinterpret_cast<void**>(&svc));
    if (FAILED(hr) || !svc) {
        Log("startup: Task Scheduler not available");
        if (SUCCEEDED(hr_init)) {
            CoUninitialize();
        }
        return false;
    }
    VARIANT empty;
    VariantInit(&empty);
    hr = svc->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) {
        Release(svc);
        if (SUCCEEDED(hr_init)) {
            CoUninitialize();
        }
        return false;
    }

    ITaskFolder* folder = nullptr;
    BSTR root = SysAllocString(L"\\");
    hr = svc->GetFolder(root, &folder);
    SysFreeString(root);
    if (FAILED(hr) || !folder) {
        Release(svc);
        if (SUCCEEDED(hr_init)) {
            CoUninitialize();
        }
        return false;
    }

    BSTR name = SysAllocString(kTaskName);
    folder->DeleteTask(name, 0);
    if (!enable) {
        SysFreeString(name);
        Release(folder);
        Release(svc);
        Log("startup: logon task removed");
        if (SUCCEEDED(hr_init)) {
            CoUninitialize();
        }
        return true;
    }

    ITaskDefinition* def = nullptr;
    hr = svc->NewTask(0, &def);
    if (FAILED(hr) || !def) {
        SysFreeString(name);
        Release(folder);
        Release(svc);
        if (SUCCEEDED(hr_init)) {
            CoUninitialize();
        }
        return false;
    }

    IPrincipal* prin = nullptr;
    if (SUCCEEDED(def->get_Principal(&prin)) && prin) {
        prin->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        prin->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        Release(prin);
    }

    ITaskSettings* settings = nullptr;
    if (SUCCEEDED(def->get_Settings(&settings)) && settings) {
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        settings->put_AllowDemandStart(VARIANT_TRUE);
        settings->put_Enabled(VARIANT_TRUE);
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        BSTR unlimited = SysAllocString(L"PT0S");
        settings->put_ExecutionTimeLimit(unlimited);
        SysFreeString(unlimited);
        Release(settings);
    }

    ITriggerCollection* triggers = nullptr;
    if (SUCCEEDED(def->get_Triggers(&triggers)) && triggers) {
        ITrigger* trigger = nullptr;
        if (SUCCEEDED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)) && trigger) {
            ILogonTrigger* logon = nullptr;
            if (SUCCEEDED(trigger->QueryInterface(IID_ILogonTrigger, reinterpret_cast<void**>(&logon))) &&
                logon) {
                logon->put_Enabled(VARIANT_TRUE);
                Release(logon);
            }
            Release(trigger);
        }
        Release(triggers);
    }

    IActionCollection* actions = nullptr;
    if (SUCCEEDED(def->get_Actions(&actions)) && actions) {
        IAction* action = nullptr;
        if (SUCCEEDED(actions->Create(TASK_ACTION_EXEC, &action)) && action) {
            IExecAction* exec = nullptr;
            if (SUCCEEDED(action->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&exec))) && exec) {
                const std::wstring path = ExePath();
                BSTR bpath = SysAllocString(path.c_str());
                exec->put_Path(bpath);
                SysFreeString(bpath);
                if (minimized) {
                    BSTR args = SysAllocString(L"--minimized");
                    exec->put_Arguments(args);
                    SysFreeString(args);
                }
                Release(exec);
            }
            Release(action);
        }
        Release(actions);
    }

    IRegisteredTask* registered = nullptr;
    hr = folder->RegisterTaskDefinition(name, def, TASK_CREATE_OR_UPDATE, empty, empty,
                                        TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    SysFreeString(name);
    Release(registered);
    Release(def);
    Release(folder);
    Release(svc);
    if (SUCCEEDED(hr_init)) {
        CoUninitialize();
    }
    if (FAILED(hr)) {
        Log("startup: failed to register logon task");
        return false;
    }
    Log(minimized ? "startup: logon task registered (minimized)" : "startup: logon task registered");
    return true;
}

}  // namespace predator
