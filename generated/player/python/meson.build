defaults_python = custom_target('defaults.py',
    input: join_paths(source_root, 'player', 'python', 'defaults.py'),
    output: 'defaults.py.inc',
    command: [file2string, '@INPUT@', '@OUTPUT@'],
)
sources += defaults_python
