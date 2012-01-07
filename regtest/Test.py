# Test
#
# Copyright (C) 2011 Carlos Garcia Campos <carlosgc@gnome.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

from hashlib import md5
import os
import subprocess
import shutil
import errno
from Config import Config

class Test:

    def __init__(self):
        self._xpstopng = os.path.join(Config().tools_dir, 'xpstopng')

    def __should_have_checksum(self, entry):
        return entry not in ('md5', 'crashed', 'failed', 'stderr');

    def create_checksums(self, refs_path, delete_refs = False):
        path = os.path.join(refs_path, 'md5')
        md5_file = open(path, 'w')

        for entry in os.listdir(refs_path):
            if not self.__should_have_checksum(entry):
                continue

            ref_path = os.path.join(refs_path, entry)
            f = open(ref_path, 'rb')
            md5_file.write("%s %s\n" % (md5(f.read()).hexdigest(), ref_path))
            f.close()
            if delete_refs:
                os.remove(ref_path)

        md5_file.close()

    def compare_checksums(self, refs_path, out_path, remove_results = True, create_diffs = True, update_refs = False):
        retval = True

        md5_path = os.path.join(refs_path, 'md5')
        md5_file = open(md5_path, 'r')
        tests = os.listdir(out_path)
        result_md5 = []

        for line in md5_file.readlines():
            md5sum, ref_path = line.strip('\n').split(' ', 1)
            basename = os.path.basename(ref_path)
            if not self.__should_have_checksum(basename):
                continue

            if not basename in tests:
                retval = False
                print("%s found in md5 ref file but missing in output dir %s" % (basename, out_path))
                continue

            result_path = os.path.join(out_path, basename)
            f = open(result_path, 'rb')
            result_md5sum = md5(f.read()).hexdigest()
            matched = md5sum == result_md5sum
            f.close()

            if update_refs:
                result_md5.append("%s %s\n" % (result_md5sum, ref_path))

            if matched:
                if remove_results:
                    os.remove(result_path)
            else:
                print("Differences found in %s" % (basename))
                if create_diffs:
                    if not os.path.exists(ref_path):
                        print("Reference file %s not found, skipping diff for %s" % (ref_path, result_path))
                    else:
                        self._create_diff(ref_path, result_path)

                if update_refs:
                    if os.path.exists(ref_path):
                        print("Updating image reference %s" % (ref_path))
                        shutil.copyfile(result_path, ref_path)
                retval = False
        md5_file.close()

        if update_refs and not retval:
            print("Updating md5 reference %s" % (md5_path))
            f = open(md5_path + '.tmp', 'wb')
            f.writelines(result_md5)
            f.close()
            os.rename(md5_path + '.tmp', md5_path)

            for ref in ('crashed', 'failed', 'stderr'):
                src = os.path.join(out_path, ref)
                dest = os.path.join(refs_path, ref)
                try:
                    shutil.copyfile(src, dest)
                except IOError as e:
                    if e.errno != errno.ENOENT:
                        raise

        return retval

    def has_md5(self, test_path):
        return os.path.exists(os.path.join(test_path, 'md5'))

    def is_crashed(self, test_path):
        return os.path.exists(os.path.join(test_path, 'crashed'))

    def is_failed(self, test_path):
        failed_path = os.path.join(test_path, 'failed')
        if not os.path.exists(failed_path):
            return 0

        f = open(failed_path, 'r')
        status = int(f.read())
        f.close()

        return status

    def has_results(self, test_path):
        return self.has_md5(test_path) or self.is_crashed(test_path) or self.is_failed(test_path)

    def has_stderr(self, test_path):
        return os.path.exists(os.path.join(test_path, 'stderr'))

    def __create_stderr_file(self, stderr, out_path):
        if not stderr:
            return

        stderr_file = open(os.path.join(out_path, 'stderr'), 'wb')
        stderr_file.write(stderr)
        stderr_file.close()

    def __create_failed_file_if_needed(self, status, out_path):
        if os.WIFEXITED(status) or os.WEXITSTATUS(status) == 0:
            return False

        failed_file = open(os.path.join(out_path, 'failed'), 'w')
        failed_file.write("%d" % (os.WEXITSTATUS(status)))
        failed_file.close()

        return True

    def _check_exit_status(self, p, out_path):
        stderr = p.stderr.read()
        status = p.wait()

        self.__create_stderr_file(stderr, out_path)

        if not os.WIFEXITED(status):
            open(os.path.join(out_path, 'crashed'), 'w').close()
            return False

        if self.__create_failed_file_if_needed(status, out_path):
            return False

        return True

    def _check_exit_status2(self, p1, p2, out_path):
        p1_stderr = p1.stderr.read()
        status1 = p1.wait()
        p2_stderr = p2.stderr.read()
        status2 = p2.wait()

        if p1_stderr or p2_stderr:
            self.__create_stderr_file(p1_stderr + p2_stderr, out_path)

        if not os.WIFEXITED(status1) or not os.WIFEXITED(status2):
            open(os.path.join(out_path, 'crashed'), 'w').close()
            return False

        if self.__create_failed_file_if_needed(status1, out_path):
            return False
        if self.__create_failed_file_if_needed(status2, out_path):
            return False

        return True

    def _create_diff(self, ref_path, result_path):
        try:
            import Image, ImageChops
        except ImportError:
            raise NotImplementedError

        ref = Image.open(ref_path)
        result = Image.open(result_path)
        diff = ImageChops.difference(ref, result)
        diff.save(result_path + '.diff', 'png')

    def create_refs(self, doc_path, refs_path):
        out_path = os.path.join(refs_path, 'page')
        p1 = subprocess.Popen([self._xpstopng, '-r', '72', '-e', doc_path, out_path], stderr = subprocess.PIPE)
        p2 = subprocess.Popen([self._xpstopng, '-r', '72', '-o', doc_path, out_path], stderr = subprocess.PIPE)

        return self._check_exit_status2(p1, p2, refs_path)

