/*
 * Wave audio file to mp3 converter using the LAME library
 *
 * Author:		Sven Sperner
 *
 * Windows (mingw/gcc-4.8.1,pthreads-w32-cvs,lame-3.99.5): 
 * build dynamic lame-lib:	./configure --prefix=/mingw && make -j{numCores}
 * compile converter dynamic:	gcc -o wav2mp3.exe wav2mp3.c -lmp3lame -lpthread
 * build static lame-lib:	./configure --prefix=/mingw --enable-static --disable-shared && make -j{numCores}
 * compile converter static:	gcc -o wav2mp3.exe wav2mp3.c -DPTW32_STATIC_LIB -static -lmp3lame -lpthread
 * compile converter static:	gcc -o wav2mp3.exe wav2mp3.c -DPTW32_STATIC_LIB -static libmp3lame.windows.a libpthread.a
 *
 * Linux/Unix (gcc-4.7.3,glibc-2.17,lame-3.99.5):
 * build dynamic lame-lib:	./configure && make -j{numCores}
 * compile converter dynamic:	gcc -o wav2mp3 wav2mp3.c -lmp3lame -lpthread
 * build static lame-lib:	./configure --enable-static --disable-shared && make -j{numCores}
 * compile converter static:	gcc -o wav2mp3 wav2mp3.c -static libmp3lame.linux.a -lpthread -lm
 * compile converter static:	gcc -o wav2mp3 wav2mp3.c -static libmp3lame.linux.a libpthread.a -lm
 * 
 * test for dynamic linking:	ldd wav2mp3(.exe)
 * strip binary from symbols:	strip wav2mp3(.exe)
 */

#include <dirent.h>
#include <lame/lame.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
	#include <windows.h>
#else
	#include <unistd.h>
#endif


#define WAV_SIZE 8192
#define MP3_SIZE 8192



/* structure for thread parameters */
typedef struct threadParams
{
	char *wavfile_path;
	char *mp3file_path;
	int *numthreads;
	pthread_mutex_t *mutex;
} threadParams;


/* function for encoding thread(s) */
void *threadFunction( void *params )
{
	/* cast the given parameter to threadParams */
	struct threadParams *threadparams = ((threadParams*)(params));


	/* print start information */
	if( pthread_mutex_lock(threadparams->mutex) == 0 )
	{
		printf( "\nBEGIN: %ld is encoding", pthread_self() );
		printf( " %s from %s\n", threadparams->mp3file_path, threadparams->wavfile_path );

		pthread_mutex_unlock( threadparams->mutex );
	}


	/* encode */
	if( encode_wav2mp3(threadparams->wavfile_path, threadparams->mp3file_path, threadparams->mutex) != EXIT_SUCCESS )
	{
		/* close thread with failure */
		pthread_exit( (void*)EXIT_FAILURE );
	}


	/* lock mutex, decrement number of running threads, unlock */
	if( pthread_mutex_lock(threadparams->mutex) == 0 )
	{
		if( *threadparams->numthreads > 0 )
		{
			*threadparams->numthreads -= 1;
		}
		else
		{
			fprintf( stderr, "%ld: ERROR numthreads !>0\n", pthread_self() );
		}

		pthread_mutex_unlock( threadparams->mutex );
	}


	/* print finish information */
	if( pthread_mutex_lock(threadparams->mutex) == 0 )
	{
		printf( "\nEND: %ld ", pthread_self() );
		printf( "has encoded %s succesfully\n", threadparams->mp3file_path, threadparams->wavfile_path );

		pthread_mutex_unlock( threadparams->mutex );
	}


	/* free */
	free( threadparams->wavfile_path );
	free( threadparams->mp3file_path );
	free( threadparams );


	/* close thread with success */
	pthread_exit( (void*)EXIT_SUCCESS );
}


/* function for encoding a wave file to a mp3 file */
int encode_wav2mp3( char *wavfile_path, char *mp3file_path, pthread_mutex_t *mutex )
{
	/* encoder flags */
	lame_global_flags *lameflags;

	/* source and destination file */
	FILE *wavfile, *mp3file;

	/* holder for return values */
	size_t bytesread, byteswrote;

	/* stream buffers */
	short *wav_buffer;
	unsigned char *mp3_buffer;

	/* channelbuffers */
	short *buffer_left;
	short *buffer_right;

	/* counters */
	unsigned long count = 0;
	int i, j;


	/* initialize the lame encoder */
	lameflags = lame_init( );

	/* default options are 2, 44.1khz, 128kbps CBR, jstereo, quality 5 */
	//lame_set_num_channels( lameflags, 2 );
	//lame_set_in_samplerate( lameflags, 44100 );
	//lame_set_brate( lameflags, 128 );
	//lame_set_mode( lameflags, 1 );	/* 0,1,2,3 = stereo, jstereo, dual channel (not supported), mono */
	//lame_set_quality( lameflags, 5 );	/* 0=best 5=good 9=worst */ 

	/* set message functions (for example for a gui application) */
	//lame_set_errorf( lameflags, error_handler_function );
	//lame_set_debugf( lameflags, debug_handler_function );
	//lame_set_msgf( lameflags, message_handler_function );

	/* set internal parameters */
	if( lame_init_params(lameflags) < 0 )
	{
		if( pthread_mutex_lock(mutex) == 0 )
		{
			fprintf( stderr, "ERROR while setting internal parameters\n" );

			pthread_mutex_unlock( mutex );
		}
	}


	/* open files */
	if( (wavfile = fopen(wavfile_path, "rb")) == NULL )
	{
		if( pthread_mutex_lock(mutex) == 0 )
		{
			fprintf( stderr, "ERROR opening wave file: %s for reading\n", wavfile_path );

			pthread_mutex_unlock( mutex );
		}

		/* return with failure */
		return( EXIT_FAILURE );
	}
	if( (mp3file = fopen(mp3file_path, "wb")) == NULL )
	{
		if( pthread_mutex_lock(mutex) == 0 )
		{
			fprintf( stderr, "ERROR opening mp3 file: %s for writing\n", mp3file_path );

			pthread_mutex_unlock( mutex );
		}

		/* return with failure */
		return( EXIT_FAILURE );
	}


	/* initialize wave & mp3 buffers */
	wav_buffer = malloc( sizeof(short) * 2 * WAV_SIZE );
	mp3_buffer = malloc( sizeof(unsigned char) * MP3_SIZE );


	/* read wave file, encode, write mp3 file */
	do
	{
		/* read wave file */
		bytesread = fread( wav_buffer, 2 * sizeof(short), WAV_SIZE, wavfile);

		/* bytes from wave file available */
		if( bytesread != 0 )
		{
			/* initialize buffers for left and right channel */
			buffer_left = malloc( sizeof(short)*bytesread );
			buffer_right = malloc( sizeof(short)*bytesread );

			/* copy / split streambuffer to channelbuffers */
			j=0;
			for( i = 0; i < bytesread; i++ )
			{
				buffer_left[i] = wav_buffer[j++];
				buffer_right[i] = wav_buffer[j++];

			}

			/* encode channelbuffers to mp3 buffer */
			/* 
			* returns number of bytes output in mp3buf. Can be 0
			* -1:  mp3buf was too small
			* -2:  malloc() problem
			* -3:  lame_init_params() not called
			* -4:  psycho acoustic problems
			*/
			byteswrote = lame_encode_buffer( lameflags, buffer_left, buffer_right, bytesread, mp3_buffer, MP3_SIZE );

			/* free channelbuffers */
			free( buffer_left );
			free( buffer_right );
		}
		/* no (more) byte from wave file available */
		else
		{
			byteswrote = lame_encode_flush( lameflags, mp3_buffer, MP3_SIZE );
		}

		/* check for encoding errors */
		if( byteswrote < 0 )
		{
			if( pthread_mutex_lock(mutex) == 0 )
			{
				fprintf( stderr, "ERROR during encoding, byteswrote: %ld\n", byteswrote );

				pthread_mutex_unlock( mutex );
			}

			/* return with failure */
			return( EXIT_FAILURE );
		}

		/* write mp3 buffer to mp3 file */
		fwrite( mp3_buffer, byteswrote, 1, mp3file );

	} while( bytesread != 0 );


	/* write mp3 tag */
	//lame_mp3_tags_fid( lameflags, mp3file );

	/* close lame flags */
	lame_close( lameflags );


	/* free memory */
	free( wav_buffer );
	free( mp3_buffer );

	/* close files */
	fclose( wavfile );
	fclose( mp3file );


	/* return with success */
	return( EXIT_SUCCESS );
}



int main( int argc, char **argv )
{
	/* source directory */
	DIR *directory;

	/* file entry in directory */
	struct dirent *entry;

	/* path/file names */
	char *path, *wavfile_path, *mp3file_path, *extension;

	/* parameters for encoding thread(s) */
	threadParams *parameters;

	/* temporary threadt */
	pthread_t tmpThread;
	
	/* mutex */
	pthread_mutex_t mutex;

	/* number of cpu cores, running threads */
	long numcores = 0;
	int numthreads = 0;


	/* get commandline arguments */
	if( argc != 2 )
	{
		fprintf( stderr, "Usage: %s <path_to_folder_with_wav_files>\n", argv[0] );

		/* exit with failure */
		return( EXIT_FAILURE );
	}
	path = malloc( strlen(argv[1]) + 2 );
	strcpy( path, argv[1] );


	/* get the used lame version */
	printf( "Using LAME Version: %s\n", get_lame_version() );


	/* check for number of cpu cores */
	#ifdef _WIN32
		SYSTEM_INFO sysinfo;
		GetSystemInfo( &sysinfo );
		numcores = sysinfo.dwNumberOfProcessors;
	#else
		numcores = sysconf( _SC_NPROCESSORS_ONLN );
	#endif
	printf( "Number of cores to use: %ld\n", numcores );



	/* open directory containing wave files */
	if( (directory = opendir(path)) == NULL )
	{
		fprintf( stderr, "ERROR opening directory: %s\n", path );

		/* exit with failure */
		return( EXIT_FAILURE );
	}


	/* initialize mutex */
	pthread_mutex_init( &mutex, NULL );


	/* run through files in directory */
	while( (entry=readdir(directory)) != NULL )
	{
		/* if file has the '.wav' extension */
		if( strcmp(strrchr(entry->d_name, '.'), ".wav") == 0 )
		{
			/* build filepaths */
			#ifdef _WIN32
				if( path[strlen(path)-1] != '\\' )
				{
					strcat( path, "\\" );
				}
			#else
				if( path[strlen(path)-1] != '/' )
				{
					strcat( path, "/" );
				}
			#endif
			wavfile_path = malloc( sizeof(char) * (strlen(path) + strlen(entry->d_name) + 1) );
			strcpy( wavfile_path, path );
			strcat( wavfile_path, entry->d_name );
			mp3file_path = malloc( sizeof(char) * (strlen(wavfile_path) + 1) );
			strcpy( mp3file_path, wavfile_path );
			extension = strrchr( mp3file_path, '.' );
			strcpy( extension, ".mp3" );
			
			/* build parameters structure for encoding-thread */
			parameters = malloc( sizeof(struct threadParams) );
			parameters->wavfile_path = malloc( strlen(wavfile_path) + 1 );
			strcpy( parameters->wavfile_path, wavfile_path );
			parameters->mp3file_path = malloc( strlen(mp3file_path) + 1 );
			strcpy( parameters->mp3file_path, mp3file_path );
			parameters->numthreads = &numthreads;
			parameters->mutex = &mutex;
			
			/* only use one thread per core */
			while( numthreads >= numcores )
			{
				printf( "\rNumber of active threads = %d, main(): sleeping", numthreads );
				#ifdef _WIN32
					Sleep( 1000 );
				#else
					sleep( 1 );
				#endif
			}

			/* encode wave file to mp3 file */
			if( pthread_create(&tmpThread, NULL, threadFunction, parameters) != 0 )
			{
				fprintf( stderr, "ERROR creating thread for %s\n", wavfile_path );
				pthread_cancel( tmpThread );
			}
			else
			{
				/* lock mutex, increment number of running threads, unlock */
				if( pthread_mutex_lock(&mutex) == 0 )
				{
					numthreads += 1;

					pthread_mutex_unlock( &mutex );
				}

				/* disassociate encoder thread from main */
 				if( pthread_detach(tmpThread) != 0 )
 				{
 				    fprintf( stderr, "ERROR detaching the encoder thread for %s from parent\n", wavfile_path );
 				}
			}
		}
		else
		{
			printf( "%s is not a wave file, skipping...\n", entry->d_name );
		}
	}


	/* wait for running theads to finish */
	while( numthreads > 0 )
	{
		printf( "\rNumber of active threads = %d, main(): wait to finish", numthreads );
		#ifdef _WIN32
			Sleep( 1000 );
		#else
			sleep( 1 );
		#endif
	}
	printf( "\nAll threads finished, closing...\n" );


	/* free memory */
	free( path );


	/* destroy mutex */
	pthread_mutex_destroy( &mutex );


	/* exit with success */
	return( EXIT_SUCCESS );
}
