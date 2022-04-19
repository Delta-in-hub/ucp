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
#include <sys/ioctl.h>

bool forceOverWrite = false, directio = true, syncio = true, verbose = false, ignoreFile = false, quiet = false;

constexpr size_t BUFFER_SIZE = 4096 * 4096 * 4;
uint8_t *buffer;

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
    fprintf(stderr, "Usage: %s [options] <source> [<source1> ...] <destination>\n", "ucp");
    fprintf(stderr, "Copy file from <sources> to <destination>\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "\t-h, --help\t\t\t\tPrint this help\n");
    fprintf(stderr, "\t-q, --quiet\t\t\t\tPrint nothing\n");
    fprintf(stderr, "\t-f, --force\t\t\t\tOverwrite exists files\n");
    fprintf(stderr, "\t-i, --ignore\t\t\t\tIgnore(Skip) exists files\n");
    fprintf(stderr, "\t-d, --directio\t\t\t\tDisable direct I/O(DMA). Default enable\n");
    fprintf(stderr, "\t-s, --sync\t\t\t\tDisable sync data after copy(flush). Default enable \n");
    fprintf(stderr, "\t-v, --verbose\t\t\t\tPrint more information\n");
    fprintf(stderr, "For more information, please visit https://github.com/Delta-in-hub/ucp\n");
}

// 0 for file , 1 for directory, -1 for error
std::pair<char, size_t> isDirOrFile(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
    {
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
size_t barLen = 5;
void printProcessBar()
{
    double percent = (copyedFileSize * 100.0) / totalFileSize;
    printf("\r[");
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
    if (path.size() < rootPath.size())
    {
        fprintf(stderr, "Error: rootPath is shorter than path\n");
        exit(EXIT_FAILURE);
    }
    if (rootPath.empty())
        return std::string(path);
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
        int r = -1;
        if (dirent->d_type == DT_UNKNOWN)
        {
            r = isDirOrFile(dirent->d_name).first;
            if (r == -1)
                fprintf(stderr, "Warning: %s is not a file or directory\n", dirent->d_name);
        }
        if (dirent->d_type == DT_DIR or r == 1)
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
        else if (dirent->d_type == DT_REG or r == 0)
        {
            std::string filePath = std::string(path);
            if (filePath.back() != '/')
                filePath.push_back('/');
            filePath.append(dirent->d_name);

            file f = {.rootPath = std::string(rootPath), .relativePath = getRelativePath(filePath, rootPath)};
            totalFileSize += isDirOrFile(filePath.data()).second;
            if (not f.rootPath.empty() and f.rootPath.back() != '/')
                f.rootPath.push_back('/');
            allFile.emplace_back(std::move(f));
        }
        else
            fprintf(stderr, "\n%s is not a file or directory,thus won't be copyed.\n", dirent->d_name);
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
            perror(i.data()); // source file not exist
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
            //./folder folder/
            std::string rpath = i;
            if (rpath.back() == '/')
                rpath.pop_back();
            auto pos = rpath.rfind('/');
            if (pos != std::string::npos)
                rpath = i.substr(0, pos + 1);
            else
                rpath.clear();
            traverseDirectory(i, rpath);
        }
    }
    totalFileNum = allFile.size();
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
        else
        {
            if (verbose)
                printf("%s created\n", file_path);
        }
        *p = '/';
    }
    return 0;
}

void copyFile(std::string_view source, std::string_view dest)
{

    struct stat srcst, destst;
    if (stat(source.data(), &srcst) == -1)
    {
        perror(source.data());
        exit(EXIT_FAILURE);
    }

    bool destExists = true;
    if (stat(dest.data(), &destst) == -1)
    {
        destExists = false;
    }

    if (destExists)
    {
        if ((srcst.st_dev == destst.st_dev) && (srcst.st_ino == destst.st_ino)) // same file
        {
            if (verbose)
                printf("%s and %s are the same file\n", source.data(), dest.data());
            copyedFileNum++;
            return;
        }
        if (forceOverWrite and !ignoreFile)
        {
            if (verbose)
                fprintf(stdout, "overwrite %s\n", dest.data());
        }
        else if (!forceOverWrite and ignoreFile)
        {
            if (verbose)
                fprintf(stdout, "skip %s\n", dest.data());
            copyedFileNum++;
            return;
        }
        else
        {
            fprintf(stdout, "\n%s is already exists.[overwrite it?][y|n] ", dest.data());
            char str[256];
            scanf("%256s", str);
            if (str[0] == 'y' || str[0] == 'Y')
            {
                if (verbose)
                    fprintf(stdout, "overwrite %s\n", dest.data());
            }
            else
            {
                if (verbose)
                    fprintf(stdout, "skip %s\n", dest.data());
                copyedFileNum++;
                return;
            }
        }
    }

    int srcFd = open(source.data(), O_RDONLY);
    if (srcFd == -1)
    {
        perror(source.data());
        exit(EXIT_FAILURE);
    }

    if (mkpath(const_cast<char *>(dest.data()), 0777) == -1)
    {
        perror("\nmkpath");
        exit(EXIT_FAILURE);
    }
    int flag = O_WRONLY | O_CREAT | O_TRUNC;
    if (directio)
        flag |= O_DIRECT;
    if (syncio)
        flag |= O_SYNC;

    int destFd = open(dest.data(), flag, srcst.st_mode);
    if (destFd == -1)
    {
        perror(dest.data());
        exit(EXIT_FAILURE);
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
            if (directio)
            {
                flag &= ~O_DIRECT;
                if (fcntl(destFd, F_SETFL, flag) == -1)
                {
                    perror("\nfcntl");
                    exit(EXIT_FAILURE);
                }
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
    if (verbose)
    {
        printf("\n");
        printf("%s copied to %s\n", source.data(), dest.data());
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

    std::vector<std::string> inputPath;
    int i = 1;
    for (; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 or strcmp(argv[i], "--help") == 0)
        {
            printUsage();
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(argv[i], "-q") == 0 or strcmp(argv[i], "--quiet") == 0)
        {
            quiet = true;
            close(1);
        }
        else if (strcmp(argv[i], "-f") == 0 or strcmp(argv[i], "--force") == 0)
        {
            forceOverWrite = true;
        }
        else if (strcmp(argv[i], "-i") == 0 or strcmp(argv[i], "--ignore") == 0)
        {
            ignoreFile = true;
        }
        else if (strcmp(argv[i], "-d") == 0 or strcmp(argv[i], "--directio") == 0)
        {
            directio = false;
        }
        else if (strcmp(argv[i], "-s") == 0 or strcmp(argv[i], "--sync") == 0)
        {
            syncio = false;
        }
        else if (strcmp(argv[i], "-v") == 0 or strcmp(argv[i], "--verbose") == 0)
        {
            verbose = true;
        }
        else
        {
            break;
        }
    }

    for (; i < argc; i++)
    {
        inputPath.push_back(argv[i]);
    }

    initAllFile(inputPath);

    std::string &destPath = inputPath.back();
    auto tmp = isDirOrFile(destPath.data());
    bool isDestDir;
    if (tmp.first == -1)
    {
        perror(destPath.data());
        exit(EXIT_FAILURE);
    }
    else if (tmp.first == 1)
    {
        isDestDir = true;
        if (destPath.back() != '/')
            destPath.push_back('/');
    }
    else
    {
        isDestDir = false;
    }

    buffer = (uint8_t *)aligned_alloc(4096, BUFFER_SIZE);
    if (buffer == nullptr)
    {
        fprintf(stderr, "\naligned_alloc failed\n");
        exit(EXIT_FAILURE);
    }

    if (!quiet)
    {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        if (w.ws_col > 45)
            barLen = w.ws_col - 45;
    }
    tm = new timer();

    for (auto &&i : allFile)
    {
        if (isDestDir)
            copyFile(i.rootPath + i.relativePath, inputPath.back() + i.relativePath);
        else
            copyFile(i.rootPath + i.relativePath, inputPath.back());
    }
    free(buffer);
    printf("\n%.2lf s, copyed %lu files, %.2lf MB to %s\n", tm->getTimeInSecond(), copyedFileNum - 1, copyedFileSize / 1000000.0, inputPath.back().data());
    delete tm;
    return 0;
}
