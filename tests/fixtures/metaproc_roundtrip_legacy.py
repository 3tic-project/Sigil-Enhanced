"""Legacy metadata editor round trip: extract with metaproc2/3, then write the pieces back unchanged."""

import metaproc2_legacy
import metaproc3_legacy


def roundtrip(opfdata, version):
    module = metaproc3_legacy if version.startswith("3") else metaproc2_legacy
    mdp = module.process_metadata(opfdata)
    pieces = ("", "", [], "") if mdp is None else (
        mdp.get_recognized_metadata(), mdp.get_other_meta_xml(), mdp.get_id_list(), mdp.get_metadata_tag())
    return module.set_new_metadata(*pieces, opfdata)
