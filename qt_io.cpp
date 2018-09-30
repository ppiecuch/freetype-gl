#include <qdebug.h>
#include <qfile.h>
#include <stdlib.h>


extern "C" bool qftglFileExits(const char *filename)
{
    return QFile::exists(filename) || QFile::exists(QString(":%1").arg(filename));
}

// (-1) if file not exists
extern "C" size_t qftglFileSize(const char *filename)
{
	long size = -1;
	QFile f(filename);
	if (f.open(QIODevice::ReadOnly)) {
	    size = f.size(); // when file does open.
	   	f.close();
	}
	return size;
}

extern "C" size_t qftglReadFile(const char *filename, char *buffer, size_t maxSize)
{
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly))
     return 0;
  return file.read(buffer, maxSize);
}
