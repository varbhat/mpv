python_files = ['defaults.py', 'spawn_off_listener.py']
foreach file: python_files
    python_file = custom_target(file,
        input: join_paths(source_root, 'player', 'python', file),
        output: file + '.inc',
        command: [file2string, '@INPUT@', '@OUTPUT@'],
    )
    sources += python_file
endforeach
