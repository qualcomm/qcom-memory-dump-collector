// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD 3-Clause Clear License
#include "FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef TOOLS_TARGET_LINUX
#include <errno.h>
#include <limits.h>
#endif

namespace QC {
namespace CLI {
#ifdef TOOLS_TARGET_LINUX
const mode_t ALL_USER_PERMISSIONS = 0777;
#endif

// ========== Cross-Platform File System Operations ==========

bool FileSystem::fileExists(const std::string& path)
{
#ifdef TOOLS_TARGET_WINDOWS
   DWORD dwAttrib = GetFileAttributesA(path.c_str());
   return (dwAttrib != INVALID_FILE_ATTRIBUTES);
#elif defined(TOOLS_TARGET_LINUX)
   struct stat buffer;
   return (stat(path.c_str(), &buffer) == 0);
#endif
}

bool FileSystem::isDirectory(const std::string& path)
{
#ifdef TOOLS_TARGET_WINDOWS
   DWORD dwAttrib = GetFileAttributesA(path.c_str());
   return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
#elif defined(TOOLS_TARGET_LINUX)
   struct stat buffer;
   if(stat(path.c_str(), &buffer) != 0)
   {
      return false;
   }
   return S_ISDIR(buffer.st_mode);
#endif
}

bool FileSystem::isFile(const std::string& path)
{
#ifdef TOOLS_TARGET_WINDOWS
   DWORD dwAttrib = GetFileAttributesA(path.c_str());
   return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
#elif defined(TOOLS_TARGET_LINUX)
   struct stat buffer;
   if(stat(path.c_str(), &buffer) != 0)
   {
      return false;
   }
   return S_ISREG(buffer.st_mode);
#endif
}

std::string FileSystem::normalizePath(const std::string& path)
{
   std::string result = path;

#ifdef TOOLS_TARGET_WINDOWS
   // Convert forward slashes to backslashes on Windows
   std::replace(result.begin(), result.end(), '/', '\\');
#elif defined(TOOLS_TARGET_LINUX)
   // Convert backslashes to forward slashes on Linux
   std::replace(result.begin(), result.end(), '\\', '/');
#endif

   return result;
}


std::string FileSystem::getAbsolutePath(const std::string& path)
{
   // If already absolute, return as-is
   if(isAbsolutePath(path))
   {
      return normalizePath(path);
   }

   // Convert relative path to absolute
#ifdef TOOLS_TARGET_WINDOWS
   char absolutePath[MAX_PATH];
   if(_fullpath(absolutePath, path.c_str(), MAX_PATH) != nullptr)
   {
      return normalizePath(std::string(absolutePath));
   }
#elif defined(TOOLS_TARGET_LINUX)
   char absolutePath[PATH_MAX];
   if(realpath(path.c_str(), absolutePath) != nullptr)
   {
      return normalizePath(std::string(absolutePath));
   }
#endif

// If realpath fails (file doesn't exist yet), manually construct absolute path
#ifdef TOOLS_TARGET_WINDOWS
   char cwd[MAX_PATH];
   if(_getcwd(cwd, sizeof(cwd)) != nullptr)
   {
      return normalizePath(joinPath(std::string(cwd), path));
   }
#elif defined(TOOLS_TARGET_LINUX)
   char cwd[PATH_MAX];
   if(getcwd(cwd, sizeof(cwd)) != nullptr)
   {
      return normalizePath(joinPath(std::string(cwd), path));
   }
#endif

   // Fallback: return normalized input path
   return normalizePath(path);
}


std::string FileSystem::joinPath(const std::string& base, const std::string& component)
{
   if(base.empty())
   {
      return component;
   }
   if(component.empty())
   {
      return base;
   }

   // Handler absolute path and networkpath in component
   if(isAbsolutePath(component))
   {
      return component;
   }
   std::string result = base;

   // Ensure base doesn't end with separator
   if(result.back() == PATH_SEPARATOR_CHAR)
   {
      result.pop_back();
   }

   // Ensure component doesn't start with separator
   std::string comp = component;
   if(comp.front() == PATH_SEPARATOR_CHAR)
   {
      comp = comp.substr(1);
   }

   return result + PATH_SEPARATOR + comp;
}

bool FileSystem::isAbsolutePath(const std::string& path)
{
   if(path.empty())
   {
      return false;
   }

#ifdef TOOLS_TARGET_WINDOWS
   // Windows: Check for drive letter (C:) or UNC path (\\)
   if(path.length() >= 2)
   {
      if(path[1] == ':' && std::isalpha(path[0]))
      {
         return true; // Drive letter like C:
      }
      if(path[0] == '\\' && path[1] == '\\')
      {
         return true; // UNC path like \\server\share
      }
   }
   return false;
#elif defined(TOOLS_TARGET_LINUX)
   // Linux: Check if starts with /
   return path[0] == '/';
#endif
}

bool FileSystem::createDirectoryWithPermissions(const std::string& path)
{
   if(!isDirectory(path))
   {
      if(!createDirectory(path))
      {
         std::cerr <<"Could not create directory: " << path 
                   <<"\nPlease try to delete /var/tmp/QFS and try again" << std::endl;
         return false;
      }
   }
#ifndef TOOLS_TARGET_WINDOWS
   if(!checkDirectoryPermissions(path, ALL_USER_PERMISSIONS))
   {
      changeFolderPermission(path, ALL_USER_PERMISSIONS);
   }
#endif
   return true;
}

// ========== Cross-Platform Temporary Directory Operations ==========
bool FileSystem::createDirectoryReclusively(const std::string& path)
{
   if(path.empty())
   {
      return false;
   }

   size_t pos = 0;
   std::string current;

   // Checking if directory already exists and has correct permissions.
   // If not, continue to create directory and set correct permissions
   if(isDirectory(path))
   {
#ifndef TOOLS_TARGET_WINDOWS
      if(checkDirectoryPermissions(path, ALL_USER_PERMISSIONS))
      {
         return true;
      }
      else
      {
         std::cerr <<
            "Directory: " + path +
            " exists, but does not have 0777 permissions\n"
            "Attempting to change permissions..." << std::endl;
      }
#else
      return true;
#endif
   }
   else
   {
      std::cerr <<
         "Directory: " + path +
         " does not exist.\n"
         "Attempting to create directory: " +
         path + " with 0777 permissions" << std::endl;
   }

   // Process intermediate directories
   while((pos = path.find_first_of("/\\", pos)) != std::string::npos)
   {
      current = path.substr(0, pos++);

      if(!current.empty())
      {
         createDirectoryWithPermissions(current);
      }
   }

   // Create final directory if missing
   createDirectoryWithPermissions(path);

// Check if final directory has correct permissions
#ifndef TOOLS_TARGET_WINDOWS
   if(!checkDirectoryPermissions(path, ALL_USER_PERMISSIONS))
   {
      throw std::runtime_error("Could not create directory: " + path + " with 0777 permissions");
   }
#endif

   return true;
}

bool FileSystem::createDirectory(const std::string& path)
{
   if(path.empty())
   {
      return false;
   }

   // Check if directory already exists
   if(isDirectory(path))
   {
      return true;
   }

#ifdef TOOLS_TARGET_WINDOWS
   return CreateDirectoryA(path.c_str(), NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
#elif defined(TOOLS_TARGET_LINUX)

   int result = mkdir(path.c_str(), ALL_USER_PERMISSIONS);
   if(result == 0)
   {
      return true; // Created successfully
   }
   if(errno == EEXIST)
   {
      return true; // Already exists
   }
   return false; // Other error

#endif
}

bool FileSystem::isDirectoryWritable(const std::string& path)
{
   if(!isDirectory(path))
   {
      return false;
   }

   // Test write permissions by creating and removing a test file
   std::string testFile = joinPath(path, "test_write_permissions");

   std::ofstream test(testFile);
   if(!test.is_open())
   {
      return false;
   }

   test << "test";
   test.close();

   // Check if file was created successfully
   bool success = fileExists(testFile);

   // Clean up test file
   if(success)
   {
      removeFile(testFile);
   }

   return success;
}

bool FileSystem::removeFile(const std::string& path)
{
   if(path.empty() || !fileExists(path))
   {
      return false;
   }

#ifdef TOOLS_TARGET_WINDOWS
   return DeleteFileA(path.c_str()) != 0;
#elif defined(TOOLS_TARGET_LINUX)
   return unlink(path.c_str()) == 0;
#endif
}

// ========== Platform-Specific Implementations ==========

#ifdef TOOLS_TARGET_LINUX

void FileSystem::changeFolderPermission(const std::string& directory, const mode_t& permissions)
{
   struct stat info;
   if(stat(directory.c_str(), &info) == 0)
   {
      if(info.st_uid == getuid())
      {
         std::ostringstream oss;
         oss << "Changing directory permissions to " << std::oct << permissions << " for " << directory << std::endl;
         std::cerr << oss.str();
         chmod(directory.c_str(), permissions);
      }
      else
      {
         std::ostringstream oss;
         oss << "Skipping permission change for " << directory << " (owned by UID " << info.st_uid << ", current UID "
             << getuid() << ")" << std::endl;
         std::cerr << oss.str();
      }
   }
   else
   {
      std::cerr <<"stat failed for " + directory << std:: endl;
   }
}


bool FileSystem::checkDirectoryPermissions(const std::string& directory, const mode_t& permissions)
{
   struct stat info;
   if(stat(directory.c_str(), &info) != 0)
   {
      std::cerr << "Unable to access directory " + directory << std::endl;
      return false;
   }

   if(S_ISDIR(info.st_mode))
   {
      if(((info.st_mode & permissions) == permissions))
      {
         return true;
      }
   }

   return false;
}

#endif

} // namespace CLI
} // namespace QC
