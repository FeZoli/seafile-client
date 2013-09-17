extern "C" {
#include <searpc-client.h>
#include <ccnet.h>

#include <searpc.h>
#include <seafile/seafile.h>
#include <seafile/seafile-object.h>

}

#include <QtDebug>
#include "seafile-applet.h"
#include "configurator.h"
#include "settings-mgr.h"

#include "utils/utils.h"
#include "local-repo.h"
#include "clone-task.h"
#include "rpc-client.h"


namespace {

const char *kSeafileRpcService = "seafile-rpcserver";
const char *kCcnetRpcService = "ccnet-rpcserver";

} // namespace

#define toCStr(_s)   ((_s).isNull() ? NULL : (_s).toUtf8().data())

SeafileRpcClient::SeafileRpcClient()
      : sync_client_(0),
        seafile_rpc_client_(0),
        ccnet_rpc_client_(0)
{
}

void SeafileRpcClient::connectDaemon()
{
    sync_client_ = ccnet_client_new();

    const QString config_dir = seafApplet->configurator()->ccnetDir();
    if (ccnet_client_load_confdir(sync_client_, toCStr(config_dir)) <  0) {
        seafApplet->errorAndExit(tr("failed to load ccnet config dir %1").arg(config_dir));
    }

    if (ccnet_client_connect_daemon(sync_client_, CCNET_CLIENT_SYNC) < 0) {
        return;
    }

    seafile_rpc_client_ = ccnet_create_rpc_client(sync_client_, NULL, kSeafileRpcService);
    ccnet_rpc_client_ = ccnet_create_rpc_client(sync_client_, NULL, kCcnetRpcService);

    qDebug("[Rpc Client] connected to daemon");
}

int SeafileRpcClient::listLocalRepos(std::vector<LocalRepo> *result)
{
    GError *error = NULL;
    GList *repos = seafile_get_repo_list(seafile_rpc_client_, 0, 0, &error);
    if (repos == NULL) {
        qWarning("failed to get repo list: %s\n", error->message);
        return -1;
    }

    for (GList *ptr = repos; ptr; ptr = ptr->next) {
        result->push_back(LocalRepo::fromGObject((GObject*)ptr->data));
    }

    g_list_foreach (repos, (GFunc)g_object_unref, NULL);
    g_list_free (repos);

    return 0;
}

int SeafileRpcClient::setAutoSync(bool autoSync)
{
    GError *error = NULL;
    if (autoSync) {
        int ret = searpc_client_call__int (seafile_rpc_client_,
                                           "seafile_enable_auto_sync",
                                           &error, 0);
        return ret;
    } else {
        int ret = searpc_client_call__int (seafile_rpc_client_,
                                           "seafile_disable_auto_sync",
                                           &error, 0);
        return ret;
    }

    return 0;
}

int SeafileRpcClient::downloadRepo(const QString &id, const QString &relayId,
                                   const QString &name, const QString &wt,
                                   const QString &token, const QString &passwd,
                                   const QString &magic, const QString &peerAddr,
                                   const QString &port, const QString &email, QString *error_ret)
{
    GError *error = NULL;
    searpc_client_call__string(
        seafile_rpc_client_,
        "seafile_download",
        &error, 10,
        "string", toCStr(id),
        "string", toCStr(relayId),
        "string", toCStr(name),
        "string", toCStr(wt),
        "string", toCStr(token),
        "string", toCStr(passwd),
        "string", toCStr(magic),
        "string", toCStr(peerAddr),
        "string", toCStr(port),
        "string", toCStr(email));

    if (error != NULL) {
        if (error_ret) {
            *error_ret = error->message;
        }
        return -1;
    }

    return 0;
}

int SeafileRpcClient::cloneRepo(const QString &id, const QString &relayId,
                                const QString &name, const QString &wt,
                                const QString &token, const QString &passwd,
                                const QString &magic, const QString &peerAddr,
                                const QString &port, const QString &email, QString *error_ret)
{
    GError *error = NULL;
    searpc_client_call__string (
        seafile_rpc_client_,
        "seafile_clone",
        &error, 10,
        "string", toCStr(id),
        "string", toCStr(relayId),
        "string", toCStr(name),
        "string", toCStr(wt),
        "string", toCStr(token),
        "string", toCStr(passwd),
        "string", toCStr(magic),
        "string", toCStr(peerAddr),
        "string", toCStr(port),
        "string", toCStr(email));

    if (error != NULL) {
        if (error_ret) {
            *error_ret = error->message;
        }
        return -1;
    }

    return 0;
}

int SeafileRpcClient::getLocalRepo(const QString& repo_id, LocalRepo *repo)
{
    GError *error = NULL;
    GObject *obj = searpc_client_call__object(
        seafile_rpc_client_,
        "seafile_get_repo",
        SEAFILE_TYPE_REPO,
        &error, 1,
        "string", toCStr(repo_id));

    if (error != NULL) {
        return -1;
    }

    if (obj == NULL) {
        return -1;
    }

    *repo = LocalRepo::fromGObject(obj);
    g_object_unref(obj);

    getSyncStatus(*repo);
    return 0;
}

int SeafileRpcClient::ccnetGetConfig(const QString &key, QString *value)
{
    GError *error = NULL;
    char *ret = searpc_client_call__string (ccnet_rpc_client_,
                                            "get_config", &error,
                                            1, "string", toCStr(key));
    if (error) {
        return -1;
    }
    *value = QString::fromUtf8(ret);
    return 0;
}

int SeafileRpcClient::seafileGetConfig(const QString &key, QString *value)
{
    GError *error = NULL;
    char *ret = searpc_client_call__string (seafile_rpc_client_,
                                            "seafile_get_config", &error,
                                            1, "string", toCStr(key));
    if (error) {
        return -1;
    }
    *value = QString::fromUtf8(ret);
    return 0;
}

int SeafileRpcClient::seafileGetConfigInt(const QString &key, int *value)
{
    GError *error = NULL;
    *value = searpc_client_call__int (seafile_rpc_client_,
                                      "seafile_get_config_int", &error,
                                      1, "string", toCStr(key));
    if (error) {
        return -1;
    }
    return 0;
}

int SeafileRpcClient::ccnetSetConfig(const QString &key, const QString &value)
{
    GError *error = NULL;
    searpc_client_call__int (ccnet_rpc_client_,
                             "set_config", &error,
                             2, "string", toCStr(key),
                             "string", toCStr(value));
    if (error) {
        return -1;
    }
    return 0;
}

int SeafileRpcClient::seafileSetConfig(const QString &key, const QString &value)
{
    GError *error = NULL;
    searpc_client_call__int (seafile_rpc_client_,
                             "seafile_set_config", &error,
                             2, "string", toCStr(key),
                             "string", toCStr(value));
    if (error) {
        return -1;
    }
    return 0;
}

int SeafileRpcClient::seafileSetConfigInt(const QString &key, int value)
{
    GError *error = NULL;
    searpc_client_call__int (seafile_rpc_client_,
                             "seafile_set_config", &error,
                             2, "string", toCStr(key),
                             "int", value);
    if (error) {
        return -1;
    }
    return 0;
}

bool SeafileRpcClient::hasLocalRepo(const QString& repo_id)
{
    LocalRepo repo;
    if (getLocalRepo(repo_id, &repo) < 0) {
        return false;
    }

    return true;
}

void SeafileRpcClient::getSyncStatus(LocalRepo &repo)
{
    if (!repo.auto_sync || !seafApplet->settingsManager()->autoSync()) {
        repo.setSyncInfo("auto sync is turned off");
        return;
    }

    GError *error = NULL;
    SeafileSyncTask *task = (SeafileSyncTask *)
        searpc_client_call__object (seafile_rpc_client_,
                                    "seafile_get_repo_sync_task",
                                    SEAFILE_TYPE_SYNC_TASK,
                                    &error, 1,
                                    "string", toCStr(repo.id));
    if (error) {
        repo.setSyncInfo("unknown");
        return;
    }

    if (!task) {
        repo.setSyncInfo("waiting for sync");
        return;
    }

    char *state = NULL;
    char *err = NULL;
    g_object_get(task, "state", &state, "error", &err, NULL);

    repo.setSyncInfo(state,
                     g_strcmp0(state, "error") == 0 ? err : NULL);

    g_free (state);
    g_free (err);
    g_object_unref(task);
}

int SeafileRpcClient::getCloneTasks(std::vector<CloneTask> *tasks)
{
    GError *error = NULL;
    GList *objlist = searpc_client_call__objlist(
        seafile_rpc_client_,
        "seafile_get_clone_tasks",
        SEAFILE_TYPE_CLONE_TASK,
        &error, 0);

    if (error) {
        return -1;
    }

    for (GList *ptr = objlist; ptr; ptr = ptr->next) {
        CloneTask task = CloneTask::fromGObject((GObject *)ptr->data);

        if (task.state == "fetch") {
            getTransferDetail(&task);
        } else if (task.state == "checkout") {
            getCheckOutDetail(&task);
        } else if (task.state == "error") {
            if (task.error_str == "fetch") {
                getTransferDetail(&task);
            }
        }
        task.translateStateInfo();
        tasks->push_back(task);
    }

    g_list_foreach (objlist, (GFunc)g_object_unref, NULL);
    g_list_free (objlist);

    return 0;
}

void SeafileRpcClient::getTransferDetail(CloneTask* task)
{
    GError *error = NULL;
    GObject *obj = searpc_client_call__object(
        seafile_rpc_client_,
        "seafile_find_transfer_task",
        SEAFILE_TYPE_TASK,
        &error, 1,
        "string", toCStr(task->repo_id));

    if (error != NULL) {
        return;
    }

    if (obj == NULL) {
        return;
    }

    if (task->state == "error") {
        char *err = NULL;
        g_object_get(obj, "error_str", &err, NULL);
        task->error_str = err;
    } else {
        int block_done = 0;
        int block_total = 0;

        g_object_get (obj,
                      "block_done", &block_done,
                      "block_total", &block_total,
                      NULL);

        task->block_done = block_done;
        task->block_total = block_total;
    }

    g_object_unref (obj);
}

void SeafileRpcClient::getCheckOutDetail(CloneTask *task)
{
    GError *error = NULL;
    GObject *obj = searpc_client_call__object(
        seafile_rpc_client_,
        "seafile_get_checkout_task",
        SEAFILE_TYPE_CHECKOUT_TASK,
        &error, 1,
        "string", toCStr(task->repo_id));

    if (error != NULL) {
        return;
    }

    if (obj == NULL) {
        return;
    }

    int checkout_done = 0;
    int checkout_total = 0;

    g_object_get (obj,
                  "total_files", &checkout_done,
                  "finished_files", &checkout_total,
                  NULL);

    task->checkout_done = checkout_done;
    task->checkout_total = checkout_total;

    g_object_unref (obj);
}

int SeafileRpcClient::cancelCloneTask(const QString& repo_id, QString *err)
{
    GError *error = NULL;
    int ret = searpc_client_call__int (seafile_rpc_client_,
                                       "seafile_cancel_clone_task",
                                       &error, 1,
                                       "string", toCStr(repo_id));

    if (ret < 0) {
        if (err) {
            *err = error ? error->message : tr("Unknown error");
        }
    }

    return ret;
}

int SeafileRpcClient::removeCloneTask(const QString& repo_id, QString *err)
{
    GError *error = NULL;
    int ret = searpc_client_call__int (seafile_rpc_client_,
                                       "seafile_remove_clone_task",
                                       &error, 1,
                                       "string", toCStr(repo_id));

    if (ret < 0) {
        if (err) {
            *err = error ? error->message : tr("Unknown error");
        }
    }

    return ret;
}

int SeafileRpcClient::getCloneTasksCount(int *count)
{
    GError *error = NULL;
    GList *objlist = searpc_client_call__objlist(
        seafile_rpc_client_,
        "seafile_get_clone_tasks",
        SEAFILE_TYPE_CLONE_TASK,
        &error, 0);

    if (error) {
        return -1;
    }

    if (count) {
        *count = g_list_length(objlist);
    }

    g_list_foreach (objlist, (GFunc)g_object_unref, NULL);
    g_list_free (objlist);

    return 0;
}
