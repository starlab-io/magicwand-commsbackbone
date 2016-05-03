import os
import string
import random

ALPHABET = string.ascii_lowercase + string.digits
FILE_LENGTH = 12
FILE_COUNT = 40000

def main():
    if not os.path.exists('test/ls/permission_denied'):
        print 'Making permission denied test directory'
        os.mkdir('test/ls/permission_denied')
        os.chmod('test/ls/permission_denied', 0) # chmod 000
    if not os.path.exists('test/ls/many_files'): # creates directory with many empty files
        print 'Making many files test directory'
        os.mkdir('test/ls/many_files')
        for i in range(FILE_COUNT):
            filename = ''.join(random.choice(ALPHABET) for x in range(FILE_LENGTH))
            with open(os.path.join('test/ls/many_files', filename), 'w') as f:
                pass
    if not os.path.exists('results'):
        print 'Setting up results directory'
        os.mkdir('results')
    pass

if __name__ == '__main__':
    main()
