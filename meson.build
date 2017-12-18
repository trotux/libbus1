project(
        'bus1',
        'c',
        version: '2',
        license: 'Apache',
        default_options: [
                'c_std=c11',
        ])

add_project_arguments('-D_GNU_SOURCE', language: 'c')
add_project_arguments('-DPACKAGE_VERSION=' + meson.project_version(), language: 'c')
add_project_arguments('-DBINDIR="' + join_paths(get_option('prefix'), get_option('bindir')) + '"', language: 'c')

cc = meson.get_compiler('c')
mod_pkgconfig = import('pkgconfig')

sub_crbtree = subproject('c-rbtree', version: '>=2')
sub_csundry = subproject('c-sundry', version: '>=1')

dep_crbtree = sub_crbtree.get_variable('libcrbtree_dep')
dep_csundry = sub_csundry.get_variable('libcsundry_dep')

subdir('src')
