/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2019 Sky UK
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
/*
 * File:   OpenCDMPlugin.cpp
 *
 */
#include "OpenCDMPlugin.h"

#include <Logging.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <fstream>


// -----------------------------------------------------------------------------
/**
 *  @brief Registers the main plugin object.
 *
 *  The object is constructed at the start of the daemon and only destructed
 *  when the daemon is shutting down.
 *
 */
REGISTER_DOBBY_PLUGIN(OpenCDMPlugin);



OpenCDMPlugin::OpenCDMPlugin(const std::shared_ptr<IDobbyEnv> &env,
                             const std::shared_ptr<IDobbyUtils> &utils)
    : mName("OpenCDM")
    , mUtilities(utils)
    , mAppsGroupId(30000)
{
    AI_LOG_FN_ENTRY();
     AI_LOG_INFO("JD constructor inside");

    // Check that we can write to the temp directory
    if (access("/tmp", W_OK) != 0)
    {
        AI_LOG_SYS_ERROR(errno, "JD Cannot access /tmp directory");
    }
     AI_LOG_INFO("JD constructor end");

    AI_LOG_FN_EXIT();
}

OpenCDMPlugin::~OpenCDMPlugin()
{
    AI_LOG_FN_ENTRY();
     AI_LOG_INFO("OpenCDM Destructor");

    AI_LOG_FN_EXIT();
}

// -----------------------------------------------------------------------------
/**
 *  @brief Boilerplate that just returns the name of the hook
 *
 *  This string needs to match the name specified in the container spec json.
 *
 */
std::string OpenCDMPlugin::name() const
{
	 AI_LOG_INFO("JD name inside");
    return mName;
}

// -----------------------------------------------------------------------------
/**
 *  @brief Indicates which hook points we want and whether to run the
 *  asynchronously or synchronously with the other hooks
 *
 *  For Netflix, the mounts should be created preStart
 */
unsigned OpenCDMPlugin::hookHints() const
{
	 AI_LOG_INFO("JD hookHints inside");
    return (IDobbyPlugin::PostConstructionSync);
}

// -----------------------------------------------------------------------------
/**
 * @brief Creates the required temp files for WPE browser to launch and decrypt
 * content. The files are created in the host filesystem and then mounted
 * into the container
 * 
 * For now, the files to create are hard coded, but could be passed in via
 * JSON in the future - FIXME
 * 
 * The JSON for the plugin should be formatted like so:
 * 
 *      {
 *          "name": "OpenCDM",
 *      }
 * 
 *  @param[in]  id              The id of the container.
 *  @param[in]  startupState    The mutable start-up state of the container.
 *  @param[in]  rootfsPath      The absolute path to the rootfs of the container.
 *  @param[in]  jsonData        The parsed json data from the container spec file.
 *
 */
bool OpenCDMPlugin::postConstruction(const ContainerId& id,
                                     const std::shared_ptr<IDobbyStartState>& startupState,
                                     const std::string& rootfsPath,
                                     const Json::Value& jsonData)
{
    AI_LOG_FN_ENTRY();
    AI_LOG_INFO("JD postconstruction inside");

    const unsigned maxBufferNum = 8;
    const unsigned mountFlags = (MS_BIND | MS_NOSUID | MS_NODEV | MS_NOEXEC);

    AI_LOG_INFO("JD Creating OCDM buffer files");

    // create the buffer files on the host file system
    for (unsigned i = 0; i < maxBufferNum; i++)
    {
        std::string path(ocdmBufferPath(i));
        std::string adminPath(ocdmBufferAdminPath(i));

        writeFileIfNotExists(path);
        writeFileIfNotExists(adminPath);

        // bind mount in the files
        if (!startupState->addMount(path, path, "bind", mountFlags))
            AI_LOG_ERROR("JD failed to add bind mount for '%s'", path.c_str());
        if (!startupState->addMount(adminPath, adminPath, "bind", mountFlags))
            AI_LOG_ERROR("JD failed to add bind mount for '%s'", adminPath.c_str());
    }

    // adjust permissions on existing /tmp/ocdm socket
    const std::string ocdmSocketPath("/tmp/ocdm");

    // sanity check the socket exists - if it doesn't then we don't mount
    if (access(ocdmSocketPath.c_str(), F_OK) != 0)
    {
        AI_LOG_ERROR("JD missing '%s' socket, not mounting in container",
                     ocdmSocketPath.c_str());
    }
    else
    {
        if (chmod(ocdmSocketPath.c_str(), S_IRWXU | S_IRGRP | S_IWGRP) != 0)
            AI_LOG_SYS_ERROR(errno, "JD failed to change access on socket");
        if (chown(ocdmSocketPath.c_str(), 0, mAppsGroupId) != 0)
            AI_LOG_SYS_ERROR(errno, "JD failed to change owner off socket");

        // mount the socket within the container
        if (!startupState->addMount(ocdmSocketPath, ocdmSocketPath, "bind", mountFlags))
            AI_LOG_ERROR("JD failed to add bind mount for '%s'", ocdmSocketPath.c_str());
    }

    // on newer builds we may also need the /tmp/OCDM directory
    enableTmpOCDMDir(startupState);
     AI_LOG_INFO("JD postconstruction end");

    AI_LOG_FN_EXIT();
    return true;
}

// -----------------------------------------------------------------------------
/**
 * @brief Ensures the /tmp/OCDM directory exists and has permissions so
 * accessible by apps but not (directory) writeable.
 *
 * This is added because on newer OCDM builds they've switched the directories
 * around.
 */
bool OpenCDMPlugin::enableTmpOCDMDir(const std::shared_ptr<IDobbyStartState>& startupState) const
{
    AI_LOG_INFO("JD enableTmpOCDMDir function inside");
    static const std::string dirPath = "/tmp/OCDM";

    // on newer builds the OCDM files have moved to a dedicate a /tmp/OCDM
    // directory
    if ((mkdir(dirPath.c_str(), 0770) != 0) && (errno != EEXIST))
    {
        AI_LOG_SYS_ERROR(errno, "JD failed to create dir @ '%s'", dirPath.c_str());
        return false;
    }

    if (chmod(dirPath.c_str(), 0770) != 0)
    {
        AI_LOG_SYS_ERROR(errno, "JD failed to change access on '%s''", dirPath.c_str());
        return false;
    }
    if (chown(dirPath.c_str(), 0, mAppsGroupId) != 0)
    {
        AI_LOG_SYS_ERROR(errno, "JD failed to change owner off '%s''", dirPath.c_str());
        return false;
    }

    // can now add to the bind mount list
    startupState->addMount(dirPath, dirPath, "bind",
                           (MS_BIND | MS_NOSUID | MS_NODEV | MS_NOEXEC));
    AI_LOG_INFO("JD enableTmpOCDMDir function end");
     return true;
 }

// -----------------------------------------------------------------------------
/**
 * @brief Returns the file path of the OCDMBuffer corresponding to the specified 
 * buffer number
 * 
 * @param[in]   bufferNum       Number of buffer to create
 */
std::string OpenCDMPlugin::ocdmBufferPath(unsigned bufferNum) const
{
	 AI_LOG_INFO("JD ocdmBufferPath function inside");
    return "/tmp/ocdmbuffer." + std::to_string(bufferNum);
}

// -----------------------------------------------------------------------------
/**
 * @brief Returns the file path of the OCDM admin Buffer corresponding to
 * the specified buffer number
 * 
 * @param[in]   bufferNum       Number of buffer to create
 */
std::string OpenCDMPlugin::ocdmBufferAdminPath(unsigned bufferNum) const
{
     AI_LOG_INFO("JD ocdmBufferAdminPath function inside");
    return "/tmp/ocdmbuffer." + std::to_string(bufferNum) +  ".admin";
}

// -----------------------------------------------------------------------------
/**
 * @brief Checks if the specified file exists then creates a blank
 * file with permissions 0760 it if it doesn't exist
 * 
 * @param[in] filePath      The file to create
 * 
 */
bool OpenCDMPlugin::writeFileIfNotExists(const std::string &filePath) const
{
    AI_LOG_FN_ENTRY();
    AI_LOG_INFO("JD writeFileIfNotExists function starting");

    // if the file already exists, don't bother recreating it
    int fileExists = access(filePath.c_str(), R_OK);

    // file doesn't exist, so lets create it
    if (fileExists < 0)
    {
        // create the file if doesn't exist
	AI_LOG_INFO("[JD][%s:%d] %s: FileName[%s]",__FILE__,__LINE__,__FUNC__,filePath.c_str());
        int fd = open(filePath.c_str(), O_CLOEXEC | O_CREAT | O_WRONLY, 0660);

        if (fd < 0)
        {
            AI_LOG_SYS_ERROR(errno, "failed to create file @ '%s'", filePath.c_str());
        }
        else
        {
	     AI_LOG_INFO("[JD][%s:%d] %s: FileName[%s] FD[%d]",__FILE__,__LINE__,__FUNC__,filePath.c_str(),fd);
            // set permissions to chmod 0660 and owner root:30000
            if (fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) != 0)
                AI_LOG_SYS_ERROR(errno, "failed to change access on '%s''", filePath.c_str());
            if (fchown(fd, 0, mAppsGroupId) != 0)
                AI_LOG_SYS_ERROR(errno, "failed to change owner off '%s''", filePath.c_str());
            if (close(fd) != 0)
                AI_LOG_SYS_ERROR(errno, "failed to close file @ '%s'", filePath.c_str());
        }

        AI_LOG_FN_EXIT();
	AI_LOG_INFO("JD writeFileIfNotExists function ending one"); 
        return true;
    }

    AI_LOG_INFO("%s JD already exists, skipping creation", filePath.c_str());
    AI_LOG_INFO("JD writeFileIfNotExists function ending two");
    AI_LOG_FN_EXIT();
    return false;
}
