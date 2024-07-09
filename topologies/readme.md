Here we store the topologies for our different routing solutions.

UCMP: topologies start with slice_. To generate the listed files, please go to ``/ucmp``.

Opera: ``dynexp_50us_10nsrc_1path.txt`` and ``dynexp_50us_10nsrc_5paths.txt``

    dynexp_50us_10nsrc_5paths.txt is too big and we provide a google link here: https://drive.google.com/file/d/1B7F3yTlNVO7C7kCwY9ym055iDyM8XVNq/view

ksp: ``general_from_dynexp_N=108_topK=1.txt`` and ``general_from_dynexp_N=108_topK=5.txt``

VLB: VLB uses random 2-hop paths at runtime and does not have static routing paths. In VLB's running script, dynexp_50us_10nsrc_1path.txt is used only for the topology information. The routing paths in this file are not used.