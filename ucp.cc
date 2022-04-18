#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <time.h>
#include <vector>
#include <string>
#include <string_view>
#include <utility>
#include <algorithm>
#include <dirent.h>

struct timer
{
    struct timespec begin, end;
    timer()
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
    }
    double getTimeInSecond()
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &end);
        return (end.tv_sec - begin.tv_sec) + (end.tv_nsec - begin.tv_nsec) / 1000000000.0;
    }
};

void printUsage()
{
    fprintf(stderr, "Usage: %s <source> [<source1>...] <destination>\n", "ucp");
    fprintf(stderr, "Copy file from <source> to <destination>.\
    \nLike using Windows to copy file to udisk.(Without writing cache of linux)\
    2022-04-18 @Delta \n");
}

// 0 for file , 1 for directory, -1 for error
std::pair<char, size_t> isDirOrFile(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
    {
        perror("stat");
        return {-1, -1};
    }
    if (S_ISDIR(st.st_mode))
        return {1, -1};
    else if (S_ISREG(st.st_mode))
        return {0, st.st_size};
    else
        return {-1, -1};
}

std::string getFileName(std::string_view path)
{
    std::string fileName;
    for (int i = path.size() - 1; i >= 0; i--)
    {
        if (path[i] == '/')
            break;
        fileName.push_back(path[i]);
    }
    std::reverse(fileName.begin(), fileName.end());
    return fileName;
}

std::string getRootPath(std::string_view path)
{
    std::string rootPath;
    int i;
    for (i = path.size() - 1; i >= 0; i--)
    {
        if (path[i] == '/')
            break;
    }
    rootPath = path.substr(0, i + 1);
    return rootPath;
}

struct file
{
    std::string rootPath;     // empty or end with '/'
    std::string relativePath; // not start with '/'
};
std::vector<file> allFile;
size_t totalFileSize = 0;
size_t totalFileNum = 0;
size_t copyedFileNum = 1;
size_t copyedFileSize = 0;

timer *tm;
void printProcessBar()
{
    double percent = (copyedFileSize * 100.0) / totalFileSize;
    printf("\r[");
    size_t barLen = 40;
    for (int i = 0; i < barLen; i++)
    {
        if (i < (copyedFileSize * barLen / totalFileSize))
            printf("=");
        else
            printf(" ");
    }
    printf("] %lu/%lu , %.2f%% , %.2lf MB/s", copyedFileNum, totalFileNum, copyedFileSize * 100.0 / totalFileSize, (double)copyedFileSize / tm->getTimeInSecond() / 1024 / 1024);

    fflush(stdout);
}

std::string getRelativePath(std::string_view path, std::string_view rootPath)
{
    if (rootPath.back() == '/')
    {
        return std::string(path.begin() + rootPath.size(), path.end());
    }
    else
    {
        return std::string(path.begin() + rootPath.size() + 1, path.end());
    }
}

void traverseDirectory(std::string_view path /*end with / */, std::string_view rootPath)
{
    DIR *dir = opendir(path.data());
    if (dir == nullptr)
    {
        perror("opendir");
        return;
    }
    struct dirent *dirent;
    while ((dirent = readdir(dir)) != nullptr)
    {
        if (dirent->d_type == DT_DIR)
        {
            if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
                continue;
            std::string filePath = std::string(path);
            if (filePath.back() != '/')
                filePath.push_back('/');
            filePath.append(dirent->d_name);
            filePath.push_back('/');
            traverseDirectory(filePath, rootPath);
        }
        else if (dirent->d_type == DT_REG)
        {
            std::string filePath = std::string(path);
            if (filePath.back() != '/')
                filePath.push_back('/');
            filePath.append(dirent->d_name);

            file f = {.rootPath = std::string(rootPath), .relativePath = getRelativePath(filePath, rootPath)};
            totalFileSize += isDirOrFile(filePath.data()).second;
            if (f.rootPath.back() != '/')
                f.rootPath.push_back('/');

            allFile.emplace_back(std::move(f));
        }
        else
        {
            fprintf(stderr, "\n%s is not a file or directory,thus won't be copyed.\n", dirent->d_name);
        }
    }
    closedir(dir);
}

void initAllFile(const std::vector<std::string> &initPath)
{

    for (size_t j = 0; j < initPath.size(); j++)
    {
        if (j + 1 == initPath.size())
        { // dest
            continue;
        }
        auto &&i = initPath[j];
        auto ret = isDirOrFile(i.c_str());
        if (ret.first == -1)
        {
            perror("\nopen source file");
            exit(EXIT_FAILURE);
        }
        else if (ret.first == 0)
        { // file
            totalFileSize += ret.second;
            file f = {.rootPath = getRootPath(i), .relativePath = getFileName(i)};
            allFile.emplace_back(std::move(f));
        }
        else
        {
            // directory
            traverseDirectory(i, i);
        }
    }
    totalFileNum = allFile.size();
}

constexpr size_t BUFFER_SIZE = 4096 * 4096 * 4;
uint8_t *buffer;

bool checkFileExists(std::string_view dest)
{
    struct stat st;
    if (stat(dest.data(), &st) == -1)
    {
        return false;
    }
    return true;
}

bool same_file(int fd1, int fd2)
{
    struct stat stat1, stat2;
    if (fstat(fd1, &stat1) < 0)
        return -1;
    if (fstat(fd2, &stat2) < 0)
        return -1;
    return (stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino);
}

int mkpath(char *file_path, mode_t mode)
{
    for (char *p = strchr(file_path + 1, '/'); p; p = strchr(p + 1, '/'))
    {
        *p = '\0';
        if (mkdir(file_path, mode) == -1)
        {
            if (errno != EEXIST)
            {
                *p = '/';
                return -1;
            }
        }
        *p = '/';
    }
    return 0;
}

void copyFile(std::string_view source, std::string_view dest)
{
    int srcFd = open(source.data(), O_RDONLY);
    if (srcFd == -1)
    {
        perror(source.data());
        exit(EXIT_FAILURE);
    }
    if (checkFileExists(dest))
    {
        fprintf(stderr, "\n%s is already exists.[overwrite it?][y|n] ", dest.data());
        char str[256];
        scanf("%256s", str);
        if (str[0] == 'y' || str[0] == 'Y')
        {
            fprintf(stderr, "overwrite %s\n", dest.data());
        }
        else
        {
            fprintf(stderr, "skip %s\n", dest.data());
            close(srcFd);
            return;
        }
    }

    if (mkpath(const_cast<char *>(dest.data()), 0777) == -1)
    {
        perror("\nmkpath");
        exit(EXIT_FAILURE);
    }
    int destFd = open(dest.data(), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC | O_DIRECT, 0666);
    if (destFd == -1)
    {
        perror(dest.data());
        exit(EXIT_FAILURE);
    }
    if (same_file(srcFd, destFd))
    {
        // fprintf(stderr, "same file %s\n", dest.data());
        close(srcFd);
        close(destFd);
        copyedFileNum++;
        return;
    }
    ssize_t readSize = 0;
    ssize_t writeSize = 0;
    bool closeDirectio = false;
    while (true)
    {
        readSize = read(srcFd, buffer, BUFFER_SIZE);
        if (readSize == 0)
            break;
        if (readSize == -1)
        {
            unlink(dest.data());
            perror(source.data());
            exit(EXIT_FAILURE);
        }
        if (readSize % 4096 != 0 and closeDirectio == false)
        {
            closeDirectio = true;
            if (fcntl(destFd, F_SETFL, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC) == -1)
            {
                perror("\nfcntl");
                exit(EXIT_FAILURE);
            }
        }
        writeSize = write(destFd, buffer, readSize);
        if (writeSize == -1)
        {
            unlink(dest.data());
            perror(dest.data());
            exit(EXIT_FAILURE);
        }
        else if (writeSize != readSize)
        {
            unlink(dest.data());
            fprintf(stderr, "\nwrite %ld bytes, but read %ld bytes\n", writeSize, readSize);
            exit(EXIT_FAILURE);
        }
        copyedFileSize += readSize;
        printProcessBar();
    }
    close(srcFd);
    close(destFd);
    copyedFileNum++;
}

int main(int argc, char *argv[])
{

    if (argc < 3)
    {
        printUsage();
        exit(EXIT_FAILURE);
    }

    std::vector<std::string> initPath;
    for (int i = 1; i < argc; i++)
    {
        initPath.emplace_back(argv[i]);
    }

    initAllFile(initPath);
    bool isDestDir;
    if (initPath.back() == ".")
    {
        initPath.back().push_back('/');
    }
    if (initPath.back().back() == '/')
        isDestDir = true;
    else
        isDestDir = false;

    if (allFile.size() > 1 and !isDestDir)
    {
        isDestDir = true;
        initPath.back().push_back('/');
    }

    buffer = (uint8_t *)aligned_alloc(4096, BUFFER_SIZE);
    if (buffer == nullptr)
    {
        fprintf(stderr, "\nbuffer is not aligned\n");
        exit(EXIT_FAILURE);
    }

    tm = new timer();

    for (auto &&i : allFile)
    {
        if (isDestDir)
            copyFile(i.rootPath + i.relativePath, initPath.back() + i.relativePath);
        else
            copyFile(i.rootPath + i.relativePath, initPath.back());
    }
    free(buffer);
    printf("\n%.2lf s, copyed %lu files, %.2lf MB to %s\n", tm->getTimeInSecond(), copyedFileNum - 1, copyedFileSize / 1000000.0, initPath.back().data());
    delete tm;
    return 0;
}
