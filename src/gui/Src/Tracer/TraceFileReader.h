#pragma once

#include "Bridge.h"
#include <QCache>
#include <QFile>
#include <atomic>
#include "TraceFileDump.h"
#include "zydis_wrapper.h"

class TraceFileParser;
class TraceFilePage;
class QZydis;
struct Instruction_t;

#define MAX_MEMORY_OPERANDS 32

class TraceFileReader : public QObject
{
    Q_OBJECT
public:
    TraceFileReader(QObject* parent = NULL);
    ~TraceFileReader();
    bool Open(const QString & fileName);
    void Close();
    bool Delete();
    bool isError(QString & reason) const;
    //int Progress() const; // TODO: Trace view should start showing its first instructions as soon as they are loaded

    QString getIndexText(TRACEINDEX index) const;

    TRACEINDEX Length() const;
    uint64_t FileSize() const;

    // Get register dump
    REGDUMP Registers(TRACEINDEX index);
    // Just get value of EIP
    duint Address(TRACEINDEX index);
    void OpCode(TRACEINDEX index, unsigned char* buffer, int* opcodeSize);
    const Instruction_t & Instruction(TRACEINDEX index);
    // Get thread ID
    DWORD ThreadId(TRACEINDEX index);
    // Get number of memory accesses
    int MemoryAccessCount(TRACEINDEX index);
    // Get memory access information. Size of these buffers are MAX_MEMORY_OPERANDS.
    void MemoryAccessInfo(TRACEINDEX index, duint* address, duint* oldMemory, duint* newMemory, bool* isValid);
    // Get hash of EXE
    duint HashValue() const;
    const QString & ExePath() const;
    QString FileName() const;

    void purgeLastPage();

    void buildDumpTo(TRACEINDEX index);
    std::vector<TRACEINDEX> getReferences(duint startAddr, duint endAddr) const;
    TraceFileDump* getDump();

signals:
    void parseFinished();

public slots:
    void parseFinishedSlot();

private slots:
    void tokenizerUpdatedSlot();

private:
    typedef std::pair<TRACEINDEX, TRACEINDEX> Range;

    QFile traceFile;
    qint64 fileSize = 0;
    TRACEINDEX length = 0;
    duint hashValue = 0;
    QString EXEPath;
    std::vector<std::pair<unsigned long long, Range>> fileIndex; //index;<file offset;length>
    std::atomic<int> progress;
    bool error = true;
    QString errorMessage;
    friend class TraceFileParser;
    friend class TraceFilePage;

    TraceFileParser* parser = nullptr;
    QCache<TRACEINDEX, TraceFilePage> pageCache;
    TraceFilePage* getPage(TRACEINDEX index, TRACEINDEX* base);
    void clearPageCache();
    TraceFileDump dump;
    void buildDump(TRACEINDEX index);

    QZydis* mDisasm;
};

duint resolveZydisRegister(const REGDUMP & registers, ZydisRegister reg);
