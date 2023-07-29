/*
Filesystem Lab disigned and implemented by Liang Junkai,RUC
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fuse.h>
#include <errno.h>
#include "disk.h"

#define DIRMODE S_IFDIR|0755
#define REGMODE S_IFREG|0644

#define SUPER 0
#define IMAP 1
#define INODE 2
#define BMAP 514
#define DATA 516

#define IBUFLEN 64
#define RECSIZE 32

typedef struct inode{
	mode_t mode;
	int size;
	unsigned short l1[14],l2,l3;
	time_t atime,mtime,ctime;
}inode;

void debug(char * buffer)
{
	printf("Debug:\n");
	for(int i=64;i<BLOCK_SIZE;++i)
		if(buffer[i])
			{
			printf("%d:%d\n",i,buffer[i]);
			break;
			}
	return;
}

//Format the virtual block device in the following function
int mkfs()
{
	char buffer[BLOCK_SIZE];
	memset(buffer,0,sizeof(buffer));
	buffer[0]|=1;
	disk_write(IMAP,buffer);
	disk_write(BMAP,buffer);
	inode ibuf[IBUFLEN];
	memset(ibuf,0,sizeof(ibuf));
	ibuf[0].mode=DIRMODE;
	ibuf[0].size=BLOCK_SIZE;
	ibuf[0].l1[0]=DATA;
	ibuf[0].atime=ibuf[0].mtime=ibuf[0].ctime=time(NULL);
	disk_write(INODE,ibuf);
	printf("Mkfs completed!\n");
	return 0;
}


int get_empty_inode()
{
	printf("Get_empty_inode is called\n");
	unsigned char buffer[BLOCK_SIZE];
	disk_read(IMAP,buffer);
	for(int i=0;i<BLOCK_SIZE;++i)
		for(int j=0;j<8;++j)
			if((buffer[i]&(1<<j))==0)
				{
				buffer[i]|=(1<<j);
				disk_write(IMAP,buffer);
				return i*8+j;
				}
	return -1;
}

int count_free_inodes()
{
	printf("Count_free_inodes is called\n");
	int ret=0;
	unsigned char buffer[BLOCK_SIZE];
	disk_read(IMAP,buffer);
	for(int i=0;i<BLOCK_SIZE;++i)
		for(int j=0;j<8;++j)
			if((buffer[i]&(1<<j))==0)
				++ret;
	return ret;
}

int get_empty_block()
{
	printf("Get_empty_block is called\n");
	unsigned char buffer[BLOCK_SIZE];
	disk_read(BMAP,buffer);
	for(int i=0;i<BLOCK_SIZE;++i)
		for(int j=0;j<8;++j)
			if((buffer[i]&(1<<j))==0)
				{
				buffer[i]|=(1<<j);
				disk_write(BMAP,buffer);
				return i*8+j;
				}
	disk_read(BMAP+1,buffer);
	for(int i=0;i<BLOCK_SIZE;++i)
		for(int j=0;j<8;++j)
			if((buffer[i]&(1<<j))==0)
				{
				buffer[i]|=(1<<j);
				disk_write(BMAP+1,buffer);
				return (BLOCK_SIZE+i)*8+j;
				}	
	return -1;
}

int count_free_blocks()
{
	printf("Count_free_blocks is called\n");
	int ret=0;
	unsigned char buffer[BLOCK_SIZE];
	disk_read(BMAP,buffer);
	for(int i=0;i<BLOCK_SIZE;++i)
		for(int j=0;j<8;++j)
			if((buffer[i]&(1<<j))==0)
				++ret;
	disk_read(BMAP+1,buffer);
	for(int i=0;i<BLOCK_SIZE;++i)
		for(int j=0;j<8;++j)
			if((buffer[i]&(1<<j))==0)	
				++ret;
	return ret-DATA;
}

void release_inode(int iid)
{
	printf("Release_inode is called:%d\n",iid);
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	memset(ibuf+iid%IBUFLEN,0,sizeof(inode));
	disk_write(INODE+iid/IBUFLEN,ibuf);
	unsigned char buffer[BLOCK_SIZE];
	disk_read(IMAP,buffer);
	buffer[iid/8]^=1<<(iid%8);
	disk_write(IMAP,buffer);
	return;
}

void release_block(int bid)
{
	printf("Release_block is called:%d\n",bid);
	unsigned char buffer[BLOCK_SIZE];
	memset(buffer,0,sizeof(buffer));
	disk_write(DATA+bid,buffer);
	int base=bid<32768?BMAP:BMAP+1;
	if(bid>=32768)bid-=32768;
	disk_read(base,buffer);
	buffer[bid/8]^=1<<(bid%8);
	disk_write(base,buffer);
	return;
}

int get_data(const inode*info,int k)
{
	printf("Get_data is called\n");
	char buffer[BLOCK_SIZE];
	if(k<14)
		return info->l1[k];
	else if(k<2062)
		{
		disk_read(info->l2,buffer);
		return *((unsigned short*)buffer+k-14);
		}
	disk_read(info->l3,buffer);
	int index=(k-2062)/2048;
	disk_read(*((unsigned short*)buffer+index),buffer);
	index=(k-2062)%2048;
	return *((unsigned short*)buffer+index);
}

int regist(inode*info,int k,int id)//No Exception Handler
{
	printf("Regist is called\n");
	char buffer[BLOCK_SIZE];
	if(k<14)
		{
		info->l1[k]=id;
		return 0;
		}
	else if(k==14)
		{
		int bid=get_empty_block();
		if(bid==-1)return 1;
		memset(buffer,0,sizeof(buffer));
		*(unsigned short*)buffer=(unsigned short)id;
		disk_write(DATA+bid,buffer);
		info->l2=DATA+bid;
		return 0;
		}
	else if(k<2062)
		{
		disk_read(info->l2,buffer);
		*((unsigned short*)buffer+k-14)=(unsigned short)id;
		disk_write(info->l2,buffer);
		return 0;
		}
	if(k==2062)
		{
		int bid=get_empty_block();
		if(bid==-1)return 1;
		info->l3=DATA+bid;
		}
	disk_read(info->l3,buffer);
	int index1=(k-2062)/2048;
	int index2=(k-2062)%2048;
	if(index2==0)
		{
		int bid=get_empty_block();
		if(bid==-1)
			{
			if(k==2062)release_block(info->l3-DATA);
			return 1;
			}
		*((unsigned short*)buffer+index1)=(unsigned short)bid;
		disk_write(info->l3,buffer);
		memset(buffer,0,sizeof(buffer));
		*(unsigned short*)buffer=(unsigned short)id;
		disk_write(DATA+bid,buffer);
		}
	else
		{
		disk_read(*((unsigned short*)buffer+index1),buffer);
		 *((unsigned short*)buffer+index2)=(unsigned short)id;
		disk_write(*((unsigned short*)buffer+index1),buffer);
		}
	return 0;
}
		
int get_inode(const char*path)
{
	printf("Get_inode is called:%s\n",path);
	if(strcmp(path,"/")==0)return 0;
	char buffer[BLOCK_SIZE];
	inode ibuf[IBUFLEN];
	char name[32];
	int now=0;
	char*start=path+1;
	char*end=strstr(start,"/");
	int cont=1;
	while(~now&&cont)
		{
		memset(name,0,sizeof(name));
		if(end)
			{
			strncpy(name,start,end-start);
			start=end+1;
			end=strstr(start,"/");
			}
		else
			{
			strcpy(name,start);
			cont=0;
			}
		disk_read(INODE+now/IBUFLEN,ibuf);
		for(int i=0;i*BLOCK_SIZE<ibuf[now%IBUFLEN].size;++i)
			{
			int flag=0;
			int data=get_data(ibuf+now%IBUFLEN,i);
			disk_read(data,buffer);
			for(int j=0;j<BLOCK_SIZE;j+=RECSIZE)
				{
				if(buffer[j]==0)
					{
					now=-1;
					flag=1;
					break;
					}
				if(strcmp(buffer+j,name)==0)
					{
					now=*(int*)(buffer+j+28);
					flag=1;
					break;
					}
				}
			if(flag)break;
			}
		}
	return now;
}

void release_block_from_inode(inode*target,int l,int r)
{
	for(int i=l;i<r;++i)
		{
		int data=get_data(target,i);
		release_block(data-DATA);
		}
	if(l<=14&&r>14)
		{
		release_block(target->l2-DATA);
		target->l2=0;
		}
	if(r>2062)
		{
		int indexl;
		if(l<=2062)indexl=0;
		else indexl=(l-15)/2048;
		int indexr=(r-2063)/2048;
		char buffer[BLOCK_SIZE];
		disk_read(target->l3,buffer);
		for(int i=indexl;i<=indexr;++i)
			{
			release_block(*((unsigned short*)buffer+i)-DATA);
			*((unsigned short*)buffer+i)=0;
			}
		if(indexl)
			disk_write(target->l3,buffer);
		else
			{
			release_block(target->l3-DATA);
			target->l3=0;
			}
		}
	return;
}

int del_link(const char *path)
{
	inode ibuf[IBUFLEN];
	char buffer[BLOCK_SIZE];
	char name[32];
	strcpy(buffer,path);
	for(int i=strlen(buffer)-1;;--i)
		if(i==1)
			{
			strcpy(name,buffer+i);
			buffer[i]=0;
			break;
			}
		else if(buffer[i]=='/')
			{
			strcpy(name,buffer+i+1);
			buffer[i]=0;
			break;
			}
	int fiid=get_inode(buffer);
	disk_read(INODE+fiid/IBUFLEN,ibuf);
	inode*target=ibuf+fiid%IBUFLEN;
	int cnt=target->size/BLOCK_SIZE-1;
	int data=get_data(target,cnt);
	disk_read(data,buffer);
	char last_name[32];
	int last_id;
	for(int i=BLOCK_SIZE-RECSIZE;i>=0;i-=RECSIZE)
		if(buffer[i])
			{
			strcpy(last_name,buffer+i);
			last_id=*(int*)(buffer+i+28);
			memset(buffer+i,0,RECSIZE);
			break;
			}
	if(buffer[0]||target->size<=BLOCK_SIZE)
		disk_write(data,buffer);
	else
		{
		target->size-=BLOCK_SIZE;
		release_block(data-DATA);
		}
	target->ctime=target->mtime=time(NULL);
	disk_write(INODE+fiid/IBUFLEN,ibuf);
	if(strcmp(last_name,name)==0)return 0;
	for(int i=0;i*BLOCK_SIZE<target->size;++i)
		{
		int flag=0;
		data=get_data(target,i);
		disk_read(data,buffer);
		for(int j=0;j<BLOCK_SIZE;j+=RECSIZE)
			if(strcmp(buffer+j,name)==0)
				{
				flag=1;
				memset(buffer+j,0,RECSIZE);
				strcpy(buffer+j,last_name);
				*(int*)(buffer+j+28)=last_id;
				break;
				}
		if(flag)
			{
			disk_write(data,buffer);
			break;
			}
		}
	return 0;
}

int add_link(const char *path,int iid)
{
	inode ibuf[IBUFLEN];
	char buffer[BLOCK_SIZE];
	char name[32];
	strcpy(buffer,path);
	for(int i=strlen(buffer)-1;;--i)
		if(i==1)
			{
			strcpy(name,buffer+i);
			buffer[i]=0;
			break;
			}
		else if(buffer[i]=='/')
			{
			strcpy(name,buffer+i+1);
			buffer[i]=0;
			break;
			}
	int fiid=get_inode(buffer);
	disk_read(INODE+fiid/IBUFLEN,ibuf);
	inode*target=ibuf+fiid%IBUFLEN;
	int cnt=target->size/BLOCK_SIZE-1;
	int data=get_data(target,cnt);
	disk_read(data,buffer);
	if(buffer[BLOCK_SIZE-RECSIZE])
		{
		int fbid=get_empty_block();
		if(fbid==-1)return 1;
		memset(buffer,0,sizeof(buffer));
		strcpy(buffer,name);
		*(int*)(buffer+28)=iid;
		disk_write(DATA+fbid,buffer);
		if(regist(target,cnt+1,DATA+fbid))
			{
			release_block(fbid);
			return 1;
			}
		else
			target->size+=BLOCK_SIZE;
		}
	else
		{
		for(int i=0;i<BLOCK_SIZE;i+=RECSIZE)
			if(buffer[i]==0)
				{
				strcpy(buffer+i,name);
				*(int*)(buffer+i+28)=iid;
				break;
				}
		disk_write(data,buffer);
		}
	target->ctime=target->mtime=time(NULL);
	disk_write(INODE+fiid/IBUFLEN,ibuf);
	return 0;
}

//Filesystem operations that you need to implement
int fs_getattr (const char *path, struct stat *attr)
{
	printf("Getattr is called:%s\n",path);
	int iid=get_inode(path);
	if(~iid)
		{
		inode ibuf[IBUFLEN];
		disk_read(INODE+iid/IBUFLEN,ibuf);
		inode*target=ibuf+iid%IBUFLEN;
		attr->st_mode=target->mode;
		attr->st_nlink=1;
		attr->st_uid=getuid();
		attr->st_gid=getgid();
		attr->st_size=target->size;
		attr->st_atime=target->atime;
		attr->st_mtime=target->mtime;
		attr->st_ctime=target->ctime;
		return 0;
		}
	printf("Not exists!\n");
	return -ENOENT;
}

int fs_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi )
{
	printf("Readdir is called:%s\n", path);
	int iid=get_inode(path);
	if(~iid)
		{
		inode ibuf[IBUFLEN];
		char buf[BLOCK_SIZE];
		disk_read(INODE+iid/IBUFLEN,ibuf);
		for(int i=0;i*BLOCK_SIZE<ibuf[iid%IBUFLEN].size;++i)
			{
			int flag=0;
			int data=get_data(ibuf+iid%IBUFLEN,i);
			disk_read(data,buf);
			for(int j=0;j<BLOCK_SIZE;j+=RECSIZE)
				{
				if(buf[j]==0)
					{
					flag=1;
					break;
					}
				filler(buffer,buf+j,NULL,0);
				}
			if(flag)break;
			}
		ibuf[iid%IBUFLEN].atime=time(NULL);
		disk_write(INODE+iid/IBUFLEN,ibuf);
		return 0;
		}
	return -ENOENT;
}

int fs_read( const char *path, char *output, size_t size, off_t offset, struct fuse_file_info *fi )
{
	printf("Read is called:%s\n",path);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	if(offset>target->size)size=0;
	else
		{
		if(offset+size>target->size)size=target->size-offset;
		char buffer[BLOCK_SIZE];
		int start=offset/BLOCK_SIZE;
		int start_pos=offset%BLOCK_SIZE;
		int end=(offset+size-1)/BLOCK_SIZE;
		int end_pos=(offset+size)%BLOCK_SIZE;
		int data=get_data(target,start);
		disk_read(data,buffer);
		if(end==start)
			strncpy(output,buffer+start_pos,size);
		else
			{
			int p=0;
			strncpy(output+p,buffer+start_pos,BLOCK_SIZE-start_pos);
			p+=BLOCK_SIZE-start_pos;
			for(int i=start+1;i<end;++i)
				{
				data=get_data(target,i);
				disk_read(data,buffer);
				strncpy(output+p,buffer,BLOCK_SIZE);
				p+=BLOCK_SIZE;
				}
			data=get_data(target,end);
			disk_read(data,buffer);
			strncpy(output+p,buffer,end_pos);
			}
		}
	target->atime=time(NULL);
	disk_write(INODE+iid/IBUFLEN,ibuf);
	return size;
}

int fs_mknod (const char *path, mode_t mode, dev_t dev)
{
	printf("Mknod is called:%s\n",path);
	int iid=get_empty_inode();
	if(iid==-1)
		{
		printf("No more inode!\n");
		return -ENOSPC;
		}
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	target->mode=REGMODE;
	target->size=0;
	target->atime=target->mtime=target->ctime=time(NULL);
	disk_write(INODE+iid/IBUFLEN,ibuf);
	
	if(add_link(path,iid))
		{
		release_inode(iid);
		return -ENOSPC;
		}
	return 0;
}

int fs_mkdir (const char *path, mode_t mode)
{
	printf("Mkdir is called:%s\n",path);
	int iid=get_empty_inode();
	if(iid==-1)
		{
		printf("No more inode!\n");
		return -ENOSPC;
		}
	int bid=get_empty_block();
	if(bid==-1)
		{
		printf("No more data block!\n");
		release_inode(iid);
		return -ENOSPC;
		}
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	target->mode=DIRMODE;
	target->size=BLOCK_SIZE;
	target->l1[0]=DATA+bid;
	target->atime=target->mtime=target->ctime=time(NULL);
	disk_write(INODE+iid/IBUFLEN,ibuf);
	
	if(add_link(path,iid))
		{
		release_inode(iid);
		release_block(bid);
		return -ENOSPC;
		}
	return 0;
}

int fs_rmdir (const char *path)
{
	printf("Rmdir is called:%s\n",path);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	if(target->size!=BLOCK_SIZE)
		printf("?????\n");//Shouldn't be executed;
	release_block(target->l1[0]-DATA);
	release_inode(iid);
	
	del_link(path);
	return 0;
}

int fs_unlink (const char *path)
{
	printf("Unlink is callded:%s\n",path);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	int cnt=(target->size+BLOCK_SIZE-1)/BLOCK_SIZE;
	release_block_from_inode(target,0,cnt);
	release_inode(iid);
	
	del_link(path);
	return 0;
}

int fs_rename (const char *path, const char *new_path)
{
	printf("Rename is called:%s\n",path);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	del_link(path);
	if(add_link(new_path,iid))
		{
		add_link(path,iid);
		return -ENOSPC;
		}
	return 0;
}

int fs_truncate (const char *path, off_t size)
{
	printf("Truncate is called:%s size:%ld\n",path,size);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	int cnt=(target->size+BLOCK_SIZE-1)/BLOCK_SIZE;
	int new_cnt=(size+BLOCK_SIZE-1)/BLOCK_SIZE;
	if(new_cnt<cnt)
		release_block_from_inode(target,new_cnt,cnt);
	else
		for(int i=cnt;i<new_cnt;++i)
			{
			int bid=get_empty_block();
			if(bid==-1)
				{
				release_block_from_inode(target,cnt,i);
				return -ENOSPC;
				}
			if(regist(target,i,bid+DATA))
				{
				release_block_from_inode(target,cnt,i);
				return -ENOSPC;
				}
			}	
	target->size=size;
	target->ctime=time(NULL);
	disk_write(INODE+iid/IBUFLEN,ibuf);
	return 0;
}

int fs_utime (const char *path, struct utimbuf *buffer)
{
	printf("Utime is called:%s\n",path);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	target->atime=buffer->actime;
	target->mtime=buffer->modtime;
	target->ctime=time(NULL);
	disk_write(INODE+iid/IBUFLEN,ibuf);
	return 0;
}

int fs_write (const char *path, const char *input, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("Write is called:%s offset:%ld\n",path,offset);
	int iid=get_inode(path);
	if(iid==-1)return -ENOENT;
	inode ibuf[IBUFLEN];
	disk_read(INODE+iid/IBUFLEN,ibuf);
	inode*target=ibuf+iid%IBUFLEN;
	if(fi->fh)offset=target->size;
	if(offset+size>target->size)
		fs_truncate(path,offset+size);
	disk_read(INODE+iid/IBUFLEN,ibuf);
	char buffer[BLOCK_SIZE];
	
	int start=offset/BLOCK_SIZE;
	int start_pos=offset%BLOCK_SIZE;
	int end=(offset+size-1)/BLOCK_SIZE;
	int end_pos=(offset+size)%BLOCK_SIZE;
	int data=get_data(target,start);
	disk_read(data,buffer);
	if(end==start)
		{
		strncpy(buffer+start_pos,input,size);
		disk_write(data,buffer);
		}
	else
		{
		int p=0;
		strncpy(buffer+start_pos,input+p,BLOCK_SIZE-start_pos);
		p+=BLOCK_SIZE-start_pos;
		disk_write(data,buffer);
		for(int i=start+1;i<end;++i)
			{
			data=get_data(target,i);
			strncpy(buffer,input+p,BLOCK_SIZE);
			p+=BLOCK_SIZE;
			disk_write(data,buffer);
			}
		data=get_data(target,end);
		disk_read(data,buffer);
		strncpy(buffer,input+p,end_pos);
		disk_write(data,buffer);
		}
	
	target->mtime=time(NULL);
	disk_write(INODE+iid/IBUFLEN,ibuf);
	return size;
}

int fs_statfs (const char *path, struct statvfs *stat)
{
	printf("Statfs is called:%s\n",path);
	stat->f_bsize=BLOCK_SIZE;
	stat->f_blocks=BLOCK_NUM-DATA;
	stat->f_bfree=stat->f_bavail=count_free_blocks();
	stat->f_files=BLOCK_SIZE*8;
	stat->f_ffree=stat->f_favail=count_free_inodes();
	stat->f_namemax=24;
	return 0;
}

int fs_open (const char *path, struct fuse_file_info *fi)
{
	printf("Open is called:%s\n",path);
	if(fi->flags&O_APPEND)
		{
		printf("Open with O_APPEND!\n");
		fi->fh=1;
		}
	else
		fi->fh=0;
	return 0;
}

//Functions you don't actually need to modify
int fs_release (const char *path, struct fuse_file_info *fi)
{
	printf("Release is called:%s\n",path);
	return 0;
}

int fs_opendir (const char *path, struct fuse_file_info *fi)
{
	printf("Opendir is called:%s\n",path);
	return 0;
}

int fs_releasedir (const char * path, struct fuse_file_info *fi)
{
	printf("Releasedir is called:%s\n",path);
	return 0;
}

static struct fuse_operations fs_operations = {
	.getattr    = fs_getattr,
	.readdir    = fs_readdir,
	.read       = fs_read,
	.mkdir      = fs_mkdir,
	.rmdir      = fs_rmdir,
	.unlink     = fs_unlink,
	.rename     = fs_rename,
	.truncate   = fs_truncate,
	.utime      = fs_utime,
	.mknod      = fs_mknod,
	.write      = fs_write,
	.statfs     = fs_statfs,
	.open       = fs_open,
	.release    = fs_release,
	.opendir    = fs_opendir,
	.releasedir = fs_releasedir
};

int main(int argc, char *argv[])
{
	if(disk_init())
		{
		printf("Can't open virtual disk!\n");
		return -1;
		}
	if(mkfs())
		{
		printf("Mkfs failed!\n");
		return -2;
		}
    return fuse_main(argc, argv, &fs_operations, NULL);
}