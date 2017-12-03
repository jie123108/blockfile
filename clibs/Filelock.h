#ifndef __BASELIB_FILELOCK_H__
#define __BASELIB_FILELOCK_H__
#include <sys/file.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

class CFilelock{
public:
	CFilelock()
	:m_file(-1)
	{
	}
	
	CFilelock(const char* filename)
	:m_file(-1)
	{
		OpenFile(filename);
	}
	~CFilelock(){CloseFile();}
	
	inline bool OpenFile(const char* filename)
	{
		assert(m_file == -1);
		strncpy(Filename, filename, sizeof(Filename));
		m_file = open(filename, O_RDWR, O_RDONLY|S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		return m_file != -1;
	}
	
	inline void CloseFile()
	{
		if(m_file > 0) {
			close(m_file);
			m_file = -1;
		}
	}

	inline void DeleteFile(){
		CloseFile();
		unlink(Filename);
	}

	inline bool Lock()
	{
		if(m_file == -1){
			return false;
		}
		return flock(m_file, LOCK_EX)==0;
	}

	inline bool TryLock()
	{
		if(m_file == -1){
			return false;
		}
		return flock(m_file, LOCK_EX | LOCK_NB)==0;
		
	}
		
	inline bool UnLock()
	{
		if(m_file == -1){
			return false;
		}
		return flock(m_file, LOCK_UN)==0;
	}

	char Filename[1024];

private:
	int m_file;
};

#endif
